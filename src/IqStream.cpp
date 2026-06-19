#include "IqStream.h"

#include "Fft.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
float ReadFloat32LE(const uint8_t *bytes) {
  uint32_t value = static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8U) |
                   (static_cast<uint32_t>(bytes[2]) << 16U) |
                   (static_cast<uint32_t>(bytes[3]) << 24U);
  float result = 0.0f;
  static_assert(sizeof(result) == sizeof(value));
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

int16_t ReadInt16LE(const uint8_t *bytes) {
  return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) |
                              (static_cast<uint16_t>(bytes[1]) << 8U));
}
} // namespace

IqStream::~IqStream() { Stop(); }

void IqStream::StartWorker(const StreamConfig &config) {
  Stop();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = {};
    snapshot_.streamRunning = true;
    snapshot_.status = "Starting";
    sampleBuffer_.clear();
    iSampleBuffer_.clear();
    qSampleBuffer_.clear();
    remainderBytes_.clear();
  }

  stopRequested_ = false;
  worker_ = std::thread([this, config]() { Run(config); });
}

void IqStream::Stop() {
  stopRequested_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
  StopCapture(); // 스트림 종료 시 캡처 파일도 닫기
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.streamRunning = false;
  snapshot_.connected = false;
  snapshot_.listening = false;
  if (snapshot_.error.empty()) {
    snapshot_.status = "Stopped";
  }
}

StreamSnapshot IqStream::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

void IqStream::AppendSamples(const std::vector<uint8_t> &bytes,
                             const StreamConfig &config) {
  const int channels = std::max(1, config.channels);
  const int channelIdx =
      std::max(0, std::min(config.channelIndex, channels - 1));

  // 샘플 포맷별 바이트 크기
  size_t bytesPerSample = 1;
  if (config.sampleFormat == SampleFormat::Int16LE) {
    bytesPerSample = 2;
  } else if (config.sampleFormat == SampleFormat::Float32LE) {
    bytesPerSample = 4;
  }
  const size_t bytesPerFrame =
      bytesPerSample * static_cast<size_t>(channels); // 1프레임 = 모든 채널

  // 이전 나머지와 현재 청크를 합침
  std::vector<uint8_t> buf;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    buf.insert(buf.end(), remainderBytes_.begin(), remainderBytes_.end());
  }
  buf.insert(buf.end(), bytes.begin(), bytes.end());

  // 완전한 프레임 수 계산
  const size_t totalFrames = buf.size() / bytesPerFrame;
  const size_t usedBytes = totalFrames * bytesPerFrame;

  std::vector<float> parsed;
  parsed.reserve(totalFrames);

  // IQ constellation: extract I(ch0) and Q(ch1) when 2+ channels available
  const bool hasIQ = channels >= 2;
  std::vector<float> iBuf, qBuf;
  // Always attempt IQ extraction if at least 2 samples per frame are available
  const bool canExtractIQ = bytesPerFrame >= bytesPerSample * 2;
  iBuf.reserve(totalFrames);
  qBuf.reserve(totalFrames);

  auto readSample = [&](const uint8_t *ptr) -> float {
    if (config.sampleFormat == SampleFormat::UInt8)
      return (static_cast<float>(*ptr) - 128.0f) / 128.0f;
    if (config.sampleFormat == SampleFormat::Int16LE)
      return static_cast<float>(ReadInt16LE(ptr)) / 32768.0f;
    return ReadFloat32LE(ptr);
  };

  for (size_t frame = 0; frame < totalFrames; ++frame) {
    const size_t frameBase = frame * bytesPerFrame;
    parsed.push_back(
        readSample(buf.data() + frameBase +
                   static_cast<size_t>(channelIdx) * bytesPerSample));
    if (canExtractIQ) {
      iBuf.push_back(readSample(buf.data() + frameBase));
      qBuf.push_back(readSample(buf.data() + frameBase + bytesPerSample));
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  // 나머지 바이트 저장 (carry-over)
  remainderBytes_.assign(buf.begin() + static_cast<std::ptrdiff_t>(usedBytes),
                         buf.end());

  if (parsed.empty()) {
    return;
  }

  const size_t maxSamples = static_cast<size_t>(config.fftSize * 4);

  if (hasIQ) {
    // IQ complex FFT path: accumulate I and Q separately
    auto trimAppend = [&](std::vector<float> &dst,
                          const std::vector<float> &src) {
      dst.insert(dst.end(), src.begin(), src.end());
      if (dst.size() > maxSamples)
        dst.erase(dst.begin(),
                  dst.end() - static_cast<std::ptrdiff_t>(maxSamples));
    };
    trimAppend(iSampleBuffer_, iBuf);
    trimAppend(qSampleBuffer_, qBuf);

    if (static_cast<int>(iSampleBuffer_.size()) >= config.fftSize) {
      const float freqOffset = freqOffsetHz_.load(std::memory_order_relaxed);
      snapshot_.magnitudesDb = Fft::MagnitudeSpectrumIQ(
          iSampleBuffer_, qSampleBuffer_, config.fftSize, config.sampleRateHz,
          freqOffset);
      const int N = static_cast<int>(snapshot_.magnitudesDb.size());
      snapshot_.frequencies.resize(static_cast<size_t>(N));
      for (int k = 0; k < N; ++k) {
        snapshot_.frequencies[static_cast<size_t>(k)] =
            (static_cast<float>(k - N / 2) * config.sampleRateHz) /
            static_cast<float>(N);
      }
      snapshot_.fftFrameCount += 1;
    }
  } else {
    // Real-only FFT fallback (mono)
    sampleBuffer_.insert(sampleBuffer_.end(), parsed.begin(), parsed.end());
    if (sampleBuffer_.size() > maxSamples)
      sampleBuffer_.erase(sampleBuffer_.begin(),
                          sampleBuffer_.end() -
                              static_cast<std::ptrdiff_t>(maxSamples));

    if (static_cast<int>(sampleBuffer_.size()) >= config.fftSize) {
      snapshot_.magnitudesDb = Fft::MagnitudeSpectrum(
          sampleBuffer_, config.fftSize, config.sampleRateHz);
      std::rotate(snapshot_.magnitudesDb.begin(),
                  snapshot_.magnitudesDb.begin() +
                      snapshot_.magnitudesDb.size() / 2,
                  snapshot_.magnitudesDb.end());
      const int N = static_cast<int>(snapshot_.magnitudesDb.size());
      snapshot_.frequencies.resize(static_cast<size_t>(N));
      for (int k = 0; k < N; ++k) {
        snapshot_.frequencies[static_cast<size_t>(k)] =
            (static_cast<float>(k) * config.sampleRateHz) /
            static_cast<float>(config.fftSize);
      }
      snapshot_.fftFrameCount += 1;
    }
  }

  // Constellation: always update if IQ bytes were available
  if (canExtractIQ && !iBuf.empty()) {
    const size_t keep = static_cast<size_t>(config.fftSize);
    auto trimAppendSnap = [&](std::vector<float> &dst,
                              const std::vector<float> &src) {
      dst.insert(dst.end(), src.begin(), src.end());
      if (dst.size() > keep)
        dst.erase(dst.begin(), dst.end() - static_cast<std::ptrdiff_t>(keep));
    };
    trimAppendSnap(snapshot_.iSamples, iBuf);
    trimAppendSnap(snapshot_.qSamples, qBuf);
  }
}

void IqStream::SetStatus(const std::string &status, const std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.status = status;
  snapshot_.error = error;
}

bool IqStream::StartCapture(const std::string &path, std::string &error) {
  std::lock_guard<std::mutex> lock(captureMutex_);
  // path는 "<base>.bin" 형태. 확장자를 떼어 payload 파일들을 담을 디렉터리로
  // 사용.
  std::filesystem::path dir(path);
  if (dir.has_extension()) {
    dir.replace_extension();
  }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    error = "Failed to create capture directory: " + dir.string();
    return false;
  }
  captureDir_ = dir.string();
  captureIndex_ = 0;
  capturing_ = true;
  {
    std::lock_guard<std::mutex> snapLock(mutex_);
    snapshot_.captureActive = true;
  }
  return true;
}

void IqStream::StopCapture() {
  std::lock_guard<std::mutex> lock(captureMutex_);
  capturing_ = false;
  captureDir_.clear();
  std::lock_guard<std::mutex> snapLock(mutex_);
  snapshot_.captureActive = false;
}

void IqStream::WriteCapture(const uint8_t *data, size_t n) {
  std::lock_guard<std::mutex> lock(captureMutex_);
  if (!capturing_ || n == 0) {
    return;
  }
  // STX/ETX 프레임 1개의 payload를 인덱스 파일 1개로 저장.
  ++captureIndex_;
  char name[32];
  std::snprintf(name, sizeof(name), "%06llu.bin",
                static_cast<unsigned long long>(captureIndex_));
  const std::filesystem::path file = std::filesystem::path(captureDir_) / name;
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) {
    return; // 개별 파일 열기 실패는 캡처 전체를 멈추지 않음
  }
  out.write(reinterpret_cast<const char *>(data),
            static_cast<std::streamsize>(n));
}
