#include "WbViewerReceiver.h"

#include <zmq.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
// WBSG 는 전부 little-endian. 호스트(x86/x64)도 LE 이므로 memcpy 로 그대로 읽는다.
template <class T> T RdLE(const uint8_t *p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}

// WbBinHeader 필드 오프셋 (ICD-0001 §4).
constexpr size_t H_MAGIC = 0;
constexpr size_t H_VERSION = 4;
constexpr size_t H_HEADER_BYTES = 8;
constexpr size_t H_SLICE_ENTRY_BYTES = 12;
constexpr size_t H_CYCLE_INDEX = 16;
constexpr size_t H_BAND_START_KHZ = 20;
constexpr size_t H_PLAN_BW_KHZ = 28;
constexpr size_t H_VALID_SAMPLES = 32;
constexpr size_t H_SLICE_COUNT = 36;
constexpr size_t H_EXPECTED_SLICES = 40;
constexpr size_t H_DC_OFFSET = 44;
constexpr size_t H_COMPLETE = 52;
constexpr size_t H_MIN_BYTES = 64; // v1 헤더 최소 크기

// WbBinSliceEntry 필드 오프셋 (ICD-0001 §5).
constexpr size_t E_CENTER_KHZ = 0;
constexpr size_t E_GRID_INDEX = 8;
constexpr size_t E_ROWS = 16;
constexpr size_t E_DATA_OFFSET = 32;

// 밴드 전체 열 수 상한(방어). expectedSlices*validSamples 가 이보다 크면 폐기.
constexpr size_t kMaxBandCols = 4u * 1024u * 1024u;
} // namespace

bool WbViewerReceiver::Start(const StreamConfig &config, std::string &error) {
  if (config.ip.empty()) {
    error = "Enter the scanner IP to connect to";
    return false;
  }
  if (config.port == 0) {
    error = "Port must be 1..65535";
    return false;
  }
  StartWorker(config);
  return true;
}

void WbViewerReceiver::Run(StreamConfig config) {
  lastCycleIndex_ = 0;
  haveLastCycle_ = false;
  dropCount_ = 0;
  cycleCount_ = 0;
  wbLinesPerCycle_ = config.wbLinesPerCycle;

  void *ctx = zmq_ctx_new();
  if (ctx == nullptr) {
    SetStatus("ZMQ init failed", "zmq_ctx_new");
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    return;
  }
  void *sub = zmq_socket(ctx, ZMQ_SUB);
  if (sub == nullptr) {
    zmq_ctx_term(ctx);
    SetStatus("ZMQ init failed", "zmq_socket");
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    return;
  }

  // 얕은 큐 + 논블로킹 수신(타임아웃)으로 최신성 우선 + 깔끔한 중단 (ICD §1.3).
  const int rcvhwm = 4;
  const int rcvtimeo = 200; // ms — stopRequested_ 재확인 주기
  const int linger = 0;
  zmq_setsockopt(sub, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
  zmq_setsockopt(sub, ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
  zmq_setsockopt(sub, ZMQ_LINGER, &linger, sizeof(linger));
  zmq_setsockopt(sub, ZMQ_SUBSCRIBE, config.wbTopic.data(),
                 config.wbTopic.size());

  const std::string endpoint =
      "tcp://" + config.ip + ":" + std::to_string(config.port);
  if (zmq_connect(sub, endpoint.c_str()) != 0) {
    SetStatus("ZMQ connect failed", zmq_strerror(zmq_errno()));
    zmq_close(sub);
    zmq_ctx_term(ctx);
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.listening = false;
    snapshot_.connected = false;
    snapshot_.status =
        "SUB connect " + endpoint + " topic=\"" + config.wbTopic + "\" (waiting)";
  }

  std::vector<uint8_t> payload; // 마지막 프레임(=WBSG payload) 누적용
  while (!stopRequested_) {
    // 멀티파트 한 메시지를 통째로 받고, 마지막 프레임을 payload 로 취한다(ICD §1.2).
    bool gotMessage = false;
    bool recvError = false;
    while (true) {
      zmq_msg_t msg;
      zmq_msg_init(&msg);
      const int rc = zmq_msg_recv(&msg, sub, 0);
      if (rc < 0) {
        zmq_msg_close(&msg);
        if (zmq_errno() == EAGAIN) {
          // 타임아웃: 메시지 중간이 아니면 정상(대기 중). stop 재확인 후 재시도.
          break;
        }
        if (zmq_errno() == ETERM || zmq_errno() == EINTR) {
          recvError = true;
        }
        break;
      }
      // 마지막 프레임이 이기도록 매번 덮어쓴다.
      const auto *bytes = static_cast<const uint8_t *>(zmq_msg_data(&msg));
      const size_t n = zmq_msg_size(&msg);
      payload.assign(bytes, bytes + n);
      gotMessage = true;

      int more = 0;
      size_t moreSize = sizeof(more);
      zmq_getsockopt(sub, ZMQ_RCVMORE, &more, &moreSize);
      zmq_msg_close(&msg);
      if (more == 0) {
        break; // 멀티파트 끝
      }
    }

    if (recvError) {
      break;
    }
    if (!gotMessage) {
      continue; // 타임아웃 — stop 재확인 후 재수신
    }

    HandleCycle(payload.data(), payload.size());
  }

  zmq_close(sub);
  zmq_ctx_term(ctx);

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.streamRunning = false;
  snapshot_.connected = false;
  snapshot_.listening = false;
  if (snapshot_.error.empty()) {
    snapshot_.status = "Stopped";
  }
}

void WbViewerReceiver::HandleCycle(const uint8_t *data, size_t len) {
  // --- 식별/버전 검사 (ICD §3.1) ---
  if (len < H_MIN_BYTES) {
    return;
  }
  if (std::memcmp(data + H_MAGIC, "WBSG", 4) != 0) {
    return;
  }
  const uint32_t version = RdLE<uint32_t>(data + H_VERSION);
  if (version != 1 && version != 2) {
    return;
  }

  const uint32_t headerBytes = RdLE<uint32_t>(data + H_HEADER_BYTES);
  const uint32_t sliceEntryBytes = RdLE<uint32_t>(data + H_SLICE_ENTRY_BYTES);
  const uint32_t cycleIndex = RdLE<uint32_t>(data + H_CYCLE_INDEX);
  const uint32_t bandStartKHz = RdLE<uint32_t>(data + H_BAND_START_KHZ);
  const uint32_t planBwKHz = RdLE<uint32_t>(data + H_PLAN_BW_KHZ);
  const uint32_t validSamples = RdLE<uint32_t>(data + H_VALID_SAMPLES);
  const uint32_t sliceCount = RdLE<uint32_t>(data + H_SLICE_COUNT);
  const uint32_t expectedSlices = RdLE<uint32_t>(data + H_EXPECTED_SLICES);
  const int32_t dcOffset = RdLE<int32_t>(data + H_DC_OFFSET);
  const uint32_t complete = RdLE<uint32_t>(data + H_COMPLETE);

  // --- 정합성 검사 (ICD §3.3: 크기 하드코딩 금지, 오프셋으로 접근) ---
  if (headerBytes < H_MIN_BYTES || sliceEntryBytes < E_DATA_OFFSET + 8 ||
      validSamples == 0 || expectedSlices == 0 || planBwKHz == 0) {
    return;
  }
  const size_t bandCols =
      static_cast<size_t>(expectedSlices) * static_cast<size_t>(validSamples);
  if (bandCols == 0 || bandCols > kMaxBandCols) {
    return;
  }
  // slice entry 표가 payload 안에 들어오는지.
  const size_t entriesEnd = static_cast<size_t>(headerBytes) +
                            static_cast<size_t>(sliceCount) *
                                static_cast<size_t>(sliceEntryBytes);
  if (entriesEnd > len) {
    return;
  }

  // --- 유효 슬라이스 수집 (bounds 검사) ---
  struct SliceRef {
    uint32_t gridIndex;
    uint32_t rows;
    uint64_t dataOffset;
  };
  std::vector<SliceRef> refs;
  refs.reserve(sliceCount);
  uint32_t maxRows = 0;
  for (uint32_t i = 0; i < sliceCount; ++i) {
    const uint8_t *e = data + headerBytes +
                       static_cast<size_t>(i) * sliceEntryBytes;
    const uint32_t gridIndex = RdLE<uint32_t>(e + E_GRID_INDEX);
    const uint32_t rows = RdLE<uint32_t>(e + E_ROWS);
    const uint64_t dataOffset = RdLE<uint64_t>(e + E_DATA_OFFSET);
    if (gridIndex >= expectedSlices || rows == 0) {
      continue;
    }
    // data 행렬이 payload 범위 안에 있는지 (ICD §8: 절대 오프셋으로 접근).
    const size_t blockBytes =
        static_cast<size_t>(rows) * static_cast<size_t>(validSamples);
    if (dataOffset > len || dataOffset + blockBytes > len) {
      continue;
    }
    refs.push_back({gridIndex, rows, dataOffset});
    maxRows = std::max(maxRows, rows);
  }
  if (refs.empty()) {
    return; // 그릴 게 없음
  }

  // --- 밴드 스펙트로그램 블록 조립 (n행 × bandCols) ---
  // 사이클 하나의 dwell 시간구조를 보존한다: 슬라이스마다 rows 행을 그대로 n행에
  // 배치. n=auto 면 사이클의 실제 최대 dwell 행 수(maxRows)를 쓴다 — 즉 압축 없이
  // 480행이면 480행 그대로. n<rows 면 구간 max-hold(버스트 보존), n>=rows 면
  // 최근접 행 복제. 슬라이스는 gridIndex 위치의 열 구간에 놓는다.
  int n = wbLinesPerCycle_ > 0 ? wbLinesPerCycle_ : static_cast<int>(maxRows);
  n = std::clamp(n, 1, 4096);

  constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
  auto block =
      std::make_shared<std::vector<float>>(static_cast<size_t>(n) * bandCols,
                                           kMissing);
  float *blk = block->data();
  float minPresent = std::numeric_limits<float>::max();

  for (const SliceRef &s : refs) {
    const uint8_t *src = data + s.dataOffset;
    const size_t c0 = static_cast<size_t>(s.gridIndex) * validSamples;
    const int rows = static_cast<int>(s.rows);
    for (int k = 0; k < n; ++k) {
      const int lo = static_cast<int>(static_cast<int64_t>(k) * rows / n);
      float *out = blk + static_cast<size_t>(k) * bandCols + c0;
      if (n >= rows) {
        // 업샘플: 최근접 원본 행 복제.
        const int srcRow = std::min(lo, rows - 1);
        const uint8_t *rp = src + static_cast<size_t>(srcRow) * validSamples;
        for (uint32_t c = 0; c < validSamples; ++c) {
          const float v =
              static_cast<float>(static_cast<int>(static_cast<int8_t>(rp[c])) -
                                 dcOffset);
          out[c] = v;
          minPresent = std::min(minPresent, v);
        }
      } else {
        // 다운샘플: [lo, hi) 구간 열별 max-hold.
        int hi = static_cast<int>(static_cast<int64_t>(k + 1) * rows / n);
        if (hi <= lo)
          hi = lo + 1;
        if (hi > rows)
          hi = rows;
        for (uint32_t c = 0; c < validSamples; ++c) {
          int best = -128;
          for (int r = lo; r < hi; ++r) {
            best = std::max(best,
                            static_cast<int>(static_cast<int8_t>(
                                src[static_cast<size_t>(r) * validSamples + c])));
          }
          const float v = static_cast<float>(best - dcOffset);
          out[c] = v;
          minPresent = std::min(minPresent, v);
        }
      }
    }
  }

  // 미수신(NaN) 열을 최솟값으로 메우고, 상단 스펙트럼 라인용 요약(열별 max-hold)을
  // 같은 패스에서 구한다.
  std::vector<float> mag(bandCols);
  for (size_t j = 0; j < bandCols; ++j) {
    float colMax = -std::numeric_limits<float>::max();
    for (int k = 0; k < n; ++k) {
      float &cell = blk[static_cast<size_t>(k) * bandCols + j];
      if (std::isnan(cell)) {
        cell = minPresent;
      }
      colMax = std::max(colMax, cell);
    }
    mag[j] = colMax;
  }

  // --- 주파수축(MHz) 조립 (ICD §6.3 / 참조 band_freqs) ---
  // 열 j 의 중심 = bandStart + (j + 0.5) * planBw / validSamples  [kHz].
  std::vector<float> freqMHz(bandCols);
  const double stepKHz =
      static_cast<double>(planBwKHz) / static_cast<double>(validSamples);
  for (size_t j = 0; j < bandCols; ++j) {
    const double fkHz =
        static_cast<double>(bandStartKHz) + (static_cast<double>(j) + 0.5) * stepKHz;
    freqMHz[j] = static_cast<float>(fkHz / 1000.0);
  }

  // --- 드롭 검출 (ICD §1.3, cycleIndex 단조 증가) ---
  if (haveLastCycle_ && cycleIndex > lastCycleIndex_ + 1) {
    dropCount_ += (cycleIndex - lastCycleIndex_ - 1);
  }
  lastCycleIndex_ = cycleIndex;
  haveLastCycle_ = true;
  ++cycleCount_;

  char status[176];
  std::snprintf(status, sizeof(status),
                "cycle #%u  slices %u/%u  %s  rows %d  drops %llu",
                cycleIndex, sliceCount, expectedSlices,
                complete ? "complete" : "partial", n,
                static_cast<unsigned long long>(dropCount_));

  // --- 스냅샷 반영 ---
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.connected = true;
  snapshot_.frequencies = std::move(freqMHz);
  snapshot_.magnitudesDb = std::move(mag);
  snapshot_.spectrogramBlock = block;   // dwell n행 (스펙트로그램에 그대로 투입)
  snapshot_.spectrogramBlockRows = n;
  snapshot_.hmftValid = true; // 광대역 축(주파수 MHz / Level) 사용
  snapshot_.fftFrameCount += 1;
  snapshot_.bytesSent += len;
  snapshot_.packetsSent = cycleCount_;
  snapshot_.status = status;
  snapshot_.error.clear();
}
