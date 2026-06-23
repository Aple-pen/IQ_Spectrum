#include "IqReceiver.h"

#include "Fft.h"
#include "TcpReceiver.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <thread>
#include <vector>

namespace {
constexpr uint32_t ChunkRequestMagic = 0x43484E4Bu; // 'CHNK'

void WriteBE32(uint32_t value, uint8_t *dst) {
  dst[0] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  dst[2] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  dst[3] = static_cast<uint8_t>(value & 0xFFU);
}
} // namespace

bool IqReceiver::Start(const StreamConfig &config, std::string &error) {
  if (config.ip.empty()) {
    error = "Enter the server IP to connect to";
    return false;
  }
  if (config.port == 0) {
    error = "Port must be 1..65535";
    return false;
  }
  if (config.chunkBytes <= 0) {
    error = "Chunk size must be greater than zero";
    return false;
  }
  if (!Fft::IsPowerOfTwo(config.fftSize)) {
    error = "FFT size must be a power of two";
    return false;
  }

  StartWorker(config);
  return true;
}

bool IqReceiver::RecvExact(TcpReceiver &client, uint8_t *dst, size_t n,
                           std::string &error) {
  size_t got = 0;
  while (got < n) {
    if (stopRequested_) {
      error.clear(); // 중단 요청: 오류 아님
      return false;
    }
    const int want = static_cast<int>(
        std::min<size_t>(n - got, static_cast<size_t>(INT_MAX)));
    const int r = client.Recv(dst + got, want, 200, error);
    if (r == -2) {
      continue; // 타임아웃: stop 여부 재확인 후 재시도
    }
    if (r == 0) {
      error = "Server closed connection";
      return false;
    }
    if (r < 0) {
      return false; // 오류: error는 Recv가 설정
    }
    got += static_cast<size_t>(r);
  }
  return true;
}

void IqReceiver::Run(StreamConfig config) {
  // 패킷 스트림 프레임: STX(4B BE) + LEN(4B BE) + payload(LEN) + ETX(4B BE)
  constexpr uint32_t kStx = 0xA0B0C0D0u;
  constexpr uint32_t kEtx = 0xD0C0B0A0u;
  // 비정상 LEN으로 인한 과도한 메모리 할당 방지 (페이로드 상한)
  constexpr size_t kMaxPayload = 512u * 1024u * 1024u; // 512 MB

  auto readBE32 = [](const uint8_t *p) -> uint32_t {
    return (static_cast<uint32_t>(p[0]) << 24U) |
           (static_cast<uint32_t>(p[1]) << 16U) |
           (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
  };

  std::vector<uint8_t> payload;
  std::vector<uint8_t> fftPendingBytes;
  const size_t minChunkBytes =
      static_cast<size_t>(std::max(config.chunkBytes, 1));

  while (!stopRequested_) {
    TcpReceiver client;
    std::string error;

    SetStatus("Connecting to " + config.ip + ":" + std::to_string(config.port));
    if (!client.Connect(config.ip, config.port, 1000, error)) {
      // 접속 실패 시 잠시 대기 후 재시도 (loop가 켜진 경우)
      SetStatus("Connect failed, retrying", error);
      if (!config.loop) {
        break;
      }
      for (int i = 0; i < 10 && !stopRequested_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      continue;
    }

    uint8_t request[8];
    WriteBE32(ChunkRequestMagic, request);
    WriteBE32(static_cast<uint32_t>(minChunkBytes), request + 4);
    if (!client.SendAll(request, sizeof(request), error)) {
      client.Close();
      SetStatus("Handshake failed, retrying", error);
      if (!config.loop) {
        break;
      }
      for (int i = 0; i < 10 && !stopRequested_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.connected = true;
      snapshot_.listening = false;
      snapshot_.status = "Connected, receiving";
      // 재접속 시 carry-over 바이트 리셋 (새 스트림 시작)
      remainderBytes_.clear();
      fftPendingBytes.clear();
    }

    bool protocolError = false;
    while (!stopRequested_) {
      // 헤더: STX(4) + LEN(4)
      uint8_t header[8];
      if (!RecvExact(client, header, sizeof(header), error)) {
        break;
      }
      const uint32_t stx = readBE32(header);
      const uint32_t len = readBE32(header + 4);
      if (stx != kStx) {
        error = "STX mismatch";
        protocolError = true;
        break;
      }
      if (len == 0 || len > kMaxPayload) {
        error = "Invalid payload length " + std::to_string(len);
        protocolError = true;
        break;
      }

      // 페이로드: LEN bytes
      payload.resize(len);
      if (!RecvExact(client, payload.data(), len, error)) {
        break;
      }

      // 푸터: ETX(4)
      uint8_t footer[4];
      if (!RecvExact(client, footer, sizeof(footer), error)) {
        break;
      }
      if (readBE32(footer) != kEtx) {
        error = "ETX mismatch";
        protocolError = true;
        break;
      }

      // 캡처 중이면 STX/ETX를 제외한 payload만 파일로 기록
      WriteCapture(payload.data(), payload.size());

      fftPendingBytes.insert(fftPendingBytes.end(), payload.begin(),
                             payload.end());
      if (fftPendingBytes.size() >= minChunkBytes) {
        AppendSamples(fftPendingBytes, config);
        fftPendingBytes.clear();
      }

      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.bytesSent += static_cast<uint64_t>(len);
      snapshot_.packetsSent += 1;
    }

    client.Close();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot_.connected = false;
      if (protocolError) {
        snapshot_.status = "Protocol error";
        snapshot_.error = error;
      } else if (!stopRequested_) {
        // RecvExact 실패(연결 종료/오류)
        snapshot_.status = error.empty() ? "Disconnected" : error;
        if (!error.empty()) {
          snapshot_.error = error;
        }
      }
    }

    if (!config.loop) {
      break;
    }
    // 프로토콜 오류 시 잠시 대기 후 재접속 (재시도 폭주 방지)
    if (protocolError) {
      for (int i = 0; i < 10 && !stopRequested_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.streamRunning = false;
  snapshot_.connected = false;
  snapshot_.listening = false;
  if (snapshot_.error.empty()) {
    snapshot_.status = "Stopped";
  }
}
