#include "IqStream.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "Fft.h"

namespace {
// channels == 1(원신호) 모드에서 int8 값에 적용하는 고정 DC offset.
constexpr int kMonoDcOffset = 70;

// HMFT 프레임 헤더: 2048 payload마다 앞에 16바이트가 삽입된다 (모두 big-endian).
//   [0]  4B Magic = "HMFT"(0x484D4654)
//   [4]  4B Sequence Counter
//   [8]  4B high user space: bit[31:29]=대역폭 코드, bit[28:0]=Center(kHz)
//   [12] 4B low user space (reserved)
constexpr uint32_t kHmftMagic        = 0x484D4654u;  // "HMFT"
constexpr size_t   kHmftHeaderBytes  = 16;
constexpr size_t   kHmftPayloadBytes = 2048;

uint32_t ReadBE32(const uint8_t *bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24U) | (static_cast<uint32_t>(bytes[1]) << 16U) |
           (static_cast<uint32_t>(bytes[2]) << 8U) | static_cast<uint32_t>(bytes[3]);
}

float ReadFloat32LE(const uint8_t *bytes) {
    uint32_t value = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
                     (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
    float result = 0.0f;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

int16_t ReadInt16LE(const uint8_t *bytes) {
    return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8U));
}
}  // namespace

IqStream::~IqStream() { Stop(); }

void IqStream::StartWorker(const StreamConfig &config) {
    Stop();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_               = {};
        snapshot_.streamRunning = true;
        snapshot_.status        = "Starting";
        sampleBuffer_.clear();
        iSampleBuffer_.clear();
        qSampleBuffer_.clear();
        remainderBytes_.clear();
    }

    stopRequested_ = false;
    worker_        = std::thread([this, config]() { Run(config); });
}

void IqStream::Stop() {
    stopRequested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    StopCapture();  // 스트림 종료 시 캡처 파일도 닫기
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    snapshot_.connected     = false;
    snapshot_.listening     = false;
    if (snapshot_.error.empty()) {
        snapshot_.status = "Stopped";
    }
}

StreamSnapshot IqStream::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}
void IqStream::SetStatus(const std::string &status, const std::string &error) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.status = status;
    snapshot_.error  = error;
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
    captureDir_   = dir.string();
    captureIndex_ = 0;
    capturing_    = true;
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
    std::snprintf(name, sizeof(name), "%06llu.bin", static_cast<unsigned long long>(captureIndex_));
    const std::filesystem::path file = std::filesystem::path(captureDir_) / name;
    std::ofstream               out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;  // 개별 파일 열기 실패는 캡처 전체를 멈추지 않음
    }
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(n));
}

void IqStream::AppendSamples(const std::vector<uint8_t> &bytes, const StreamConfig &config) {
    const int channels   = std::max(1, config.channels);
    const int channelIdx = std::max(0, std::min(config.channelIndex, channels - 1));

    // 샘플 포맷별 바이트 크기
    size_t bytesPerSample = 1;
    if (config.sampleFormat == SampleFormat::Int16LE) {
        bytesPerSample = 2;
    } else if (config.sampleFormat == SampleFormat::Float32LE) {
        bytesPerSample = 4;
    }
    const size_t bytesPerFrame = bytesPerSample * static_cast<size_t>(channels);  // 1프레임 = 모든 채널

    // 이전 나머지와 현재 청크를 합침
    std::vector<uint8_t> buf;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buf.insert(buf.end(), remainderBytes_.begin(), remainderBytes_.end());
    }
    buf.insert(buf.end(), bytes.begin(), bytes.end());

    // HMFT 프레임 헤더 처리: payload 스트림에서 magic("HMFT")을 스캔해 16바이트
    // 헤더를 벗겨내고 payload(2048B)만 남긴다. chunk 경계로 프레임이 잘려도
    // remainder로 이월한다. (config.hmftHeader가 꺼져 있으면 buf를 그대로 사용)
    // HMFT 헤더는 광대역 스캔(1채널) 모드 전용이므로 channels == 1일 때만 적용.
    const bool           hmftMode = config.hmftHeader && channels == 1;
    std::vector<uint8_t> hmftCarry;
    bool                 hmftAny  = false;
    uint32_t             hmftSeq  = 0;
    uint32_t             hmftHigh = 0;
    if (hmftMode) {
        auto matchMagic = [&](size_t i) -> bool {
            return i + 4 <= buf.size() && ReadBE32(buf.data() + i) == kHmftMagic;
        };
        std::vector<uint8_t> payload;
        payload.reserve(buf.size());
        size_t pos = 0;
        while (true) {
            // pos부터 magic 위치 탐색 (초기 정렬 및 바이트 유실 시 재동기화)
            size_t m = pos;
            while (m + 4 <= buf.size() && !matchMagic(m)) {
                ++m;
            }
            if (m + 4 > buf.size()) {
                // 완전한 magic 없음: 뒤쪽 최대 3바이트(부분 magic 가능성)만 이월
                const size_t keep = std::min<size_t>(3, buf.size() - pos);
                hmftCarry.assign(buf.end() - static_cast<std::ptrdiff_t>(keep), buf.end());
                break;
            }
            if (m + kHmftHeaderBytes + kHmftPayloadBytes > buf.size()) {
                // 프레임(헤더+payload)이 아직 덜 도착: magic부터 통째로 이월
                hmftCarry.assign(buf.begin() + static_cast<std::ptrdiff_t>(m), buf.end());
                break;
            }
            // 헤더 파싱 후 payload(2048B) 추출
            hmftSeq                   = ReadBE32(buf.data() + m + 4);
            hmftHigh                  = ReadBE32(buf.data() + m + 8);
            hmftAny                   = true;
            const size_t payloadStart = m + kHmftHeaderBytes;
            payload.insert(payload.end(), buf.begin() + static_cast<std::ptrdiff_t>(payloadStart),
                           buf.begin() + static_cast<std::ptrdiff_t>(payloadStart + kHmftPayloadBytes));
            pos = payloadStart + kHmftPayloadBytes;
        }
        buf = std::move(payload);
    }

    // 완전한 프레임 수 계산
    const size_t totalFrames = buf.size() / bytesPerFrame;
    const size_t usedBytes   = totalFrames * bytesPerFrame;

    std::vector<float> parsed;
    parsed.reserve(totalFrames);

    // IQ constellation: extract I(ch0) and Q(ch1) when 2+ channels available
    const bool         hasIQ = channels >= 2;
    std::vector<float> iBuf, qBuf;
    // Always attempt IQ extraction if at least 2 samples per frame are available
    const bool canExtractIQ = bytesPerFrame >= bytesPerSample * 2;
    iBuf.reserve(totalFrames);
    qBuf.reserve(totalFrames);

    auto readSample = [&](const uint8_t *ptr) -> float {
        if (config.sampleFormat == SampleFormat::UInt8) return (static_cast<float>(*ptr) - 128.0f) / 128.0f;
        if (config.sampleFormat == SampleFormat::Int16LE) return static_cast<float>(ReadInt16LE(ptr)) / 32768.0f;
        return ReadFloat32LE(ptr);
    };

    for (size_t frame = 0; frame < totalFrames; ++frame) {
        const size_t frameBase = frame * bytesPerFrame;
        parsed.push_back(readSample(buf.data() + frameBase + static_cast<size_t>(channelIdx) * bytesPerSample));
        if (canExtractIQ) {
            iBuf.push_back(readSample(buf.data() + frameBase));
            qBuf.push_back(readSample(buf.data() + frameBase + bytesPerSample));
        }
        const size_t bytesPerFrame = bytesPerSample * static_cast<size_t>(channels);  // 1프레임 = 모든 채널

        std::lock_guard<std::mutex> lock(mutex_);
        // 나머지 바이트 저장 (carry-over)
        if (hmftMode) {
            // HMFT 모드: 헤더 단위로 잘린 미완성 프레임을 이월 (payload는 2048의 배수라
            // 샘플 단위 잔여는 없음).
            remainderBytes_ = std::move(hmftCarry);
        } else {
            remainderBytes_.assign(buf.begin() + static_cast<std::ptrdiff_t>(usedBytes), buf.end());
        }

        // 파싱한 HMFT 헤더 정보를 스냅샷에 반영 (UI 표시용)
        if (hmftMode && hmftAny) {
            snapshot_.hmftValid     = true;
            snapshot_.hmftSeq       = hmftSeq;
            snapshot_.hmftBwCode    = static_cast<int>((hmftHigh >> 29U) & 0x7U);
            snapshot_.hmftCenterKHz = hmftHigh & 0x1FFFFFFFU;
        }

        // 완전한 프레임 수 계산
        const size_t totalFrames = buf.size() / bytesPerFrame;
        const size_t usedBytes   = totalFrames * bytesPerFrame;

        std::vector<float> parsed;
        parsed.reserve(totalFrames);

        // IQ constellation: extract I(ch0) and Q(ch1) when 2+ channels available
        const bool         hasIQ = channels >= 2;
        std::vector<float> iBuf, qBuf;
        // Always attempt IQ extraction if at least 2 samples per frame are available
        const bool canExtractIQ = bytesPerFrame >= bytesPerSample * 2;
        iBuf.reserve(totalFrames);
        qBuf.reserve(totalFrames);

        auto readSample = [&](const uint8_t *ptr) -> float {
            if (config.sampleFormat == SampleFormat::UInt8) return (static_cast<float>(*ptr) - 128.0f) / 128.0f;
            if (config.sampleFormat == SampleFormat::Int16LE) return static_cast<float>(ReadInt16LE(ptr)) / 32768.0f;
            return ReadFloat32LE(ptr);
        };

        for (size_t frame = 0; frame < totalFrames; ++frame) {
            const size_t frameBase = frame * bytesPerFrame;
            parsed.push_back(readSample(buf.data() + frameBase + static_cast<size_t>(channelIdx) * bytesPerSample));
            if (canExtractIQ) {
                iBuf.push_back(readSample(buf.data() + frameBase));
                qBuf.push_back(readSample(buf.data() + frameBase + bytesPerSample));
            }
        }

        if (static_cast<int>(sampleBuffer_.size()) >= config.fftSize) {
            const size_t N = static_cast<size_t>(config.fftSize);
            // 최신 N개 샘플을 그대로 y값으로 사용 (FFT/dB 변환 없음)
            snapshot_.magnitudesDb.assign(sampleBuffer_.end() - static_cast<std::ptrdiff_t>(N), sampleBuffer_.end());
            // x축: HMFT 헤더가 있으면 center±BW/2 실제 주파수(MHz)로, 없으면 0..1 정규화.
            snapshot_.frequencies.resize(N);
            const float denom = N > 1 ? static_cast<float>(N - 1) : 1.0f;
            if (hmftMode && hmftAny) {
                // 대역폭 코드 -> MHz (200M=0,100M=1,20M=2,10M=3,5M=4)
                static const float kBwMHz[]  = {200.0f, 100.0f, 20.0f, 10.0f, 5.0f};
                const int          bwCode    = static_cast<int>((hmftHigh >> 29U) & 0x7U);
                const float        bwMHz     = (bwCode >= 0 && bwCode < 5) ? kBwMHz[bwCode] : 1.0f;
                const float        centerMHz = static_cast<float>(hmftHigh & 0x1FFFFFFFU) / 1000.0f;
                const float        startMHz  = centerMHz - bwMHz * 0.5f;
                for (size_t k = 0; k < N; ++k) {
                    snapshot_.frequencies[k] = startMHz + bwMHz * static_cast<float>(k) / denom;
                }
            } else {
                // python의 linspace(0,1,N)와 동일
                for (size_t k = 0; k < N; ++k) {
                    snapshot_.frequencies[k] = static_cast<float>(k) / denom;
                }
            }
            snapshot_.fftFrameCount += 1;
        }

        if (hasIQ) {
            const size_t maxSamples = static_cast<size_t>(config.fftSize * 4);
            // IQ complex FFT path: accumulate I and Q separately
            auto trimAppend = [&](std::vector<float> &dst, const std::vector<float> &src) {
                dst.insert(dst.end(), src.begin(), src.end());
                if (dst.size() > maxSamples)
                    dst.erase(dst.begin(), dst.end() - static_cast<std::ptrdiff_t>(maxSamples));
            };
            trimAppend(iSampleBuffer_, iBuf);
            trimAppend(qSampleBuffer_, qBuf);

            if (static_cast<int>(iSampleBuffer_.size()) >= config.fftSize) {
                const float freqOffset = freqOffsetHz_.load(std::memory_order_relaxed);
                snapshot_.magnitudesDb = Fft::MagnitudeSpectrumIQ(iSampleBuffer_, qSampleBuffer_, config.fftSize,
                                                                  config.sampleRateHz, freqOffset);
                const int N            = static_cast<int>(snapshot_.magnitudesDb.size());
                snapshot_.frequencies.resize(static_cast<size_t>(N));
                for (int k = 0; k < N; ++k) {
                    snapshot_.frequencies[static_cast<size_t>(k)] =
                        (static_cast<float>(k - N / 2) * config.sampleRateHz) / static_cast<float>(N);
                }
                snapshot_.fftFrameCount += 1;
            }
        } else {
            // channels == 1: FFT를 하지 않고 원신호(int8) 값을 그대로 그린다 (시간영역
            // 파형). 값은 정규화하지 않고 int8(-128..127)에서 DC offset(-70)만 뺀 고정
            // 스케일을 사용한다.
            // 참고 python: v = np.fromfile(..., dtype=np.int8) - 70; plt.plot(x, v)

            // ***** 광대역 스캐너 정책 *****
            // **[16바이트 헤더 + 2048 byte per frame]**
            // MagicNumber : 4byte (0x484D4654 'HMFT')
            // Sequence Counter : 4byte (DMA fifo reset 되면 0으로 초기화)
            // user space : 4byte ([31~29]상위 3bit :Scan 대역폭, [28~0] 29bit: 현재 Center 주파수(khz단위))
            // reserved : 4byte (0x00000000)
            const size_t maxSamples = static_cast<size_t>(config.fftSize + 16);
            for (size_t frame = 0; frame < totalFrames; ++frame) {
                const size_t idx = frame * bytesPerFrame + static_cast<size_t>(channelIdx) * bytesPerSample;
                const int    raw = static_cast<int>(static_cast<int8_t>(buf[idx]));
                sampleBuffer_.push_back(static_cast<float>(raw - kMonoDcOffset));
            }
            if (sampleBuffer_.size() > maxSamples)
                sampleBuffer_.erase(sampleBuffer_.begin(),
                                    sampleBuffer_.end() - static_cast<std::ptrdiff_t>(maxSamples));

            if (static_cast<int>(sampleBuffer_.size()) >= config.fftSize + 16) {
                const size_t N = static_cast<size_t>(config.fftSize + 16);
                // 최신 N개 샘플을 그대로 y값으로 사용 (FFT/dB 변환 없음)
                snapshot_.magnitudesDb.assign(sampleBuffer_.end() - static_cast<std::ptrdiff_t>(N),
                                              sampleBuffer_.end());
                // x축: 0..1 정규화 (python의 linspace(0,1,N)와 동일)
                snapshot_.frequencies.resize(N);
                const float denom = N > 1 ? static_cast<float>(N - 1) : 1.0f;
                for (size_t k = 0; k < N; ++k) {
                    snapshot_.frequencies[k] = static_cast<float>(k) / denom;
                }
                snapshot_.fftFrameCount += 1;
            }
        }

        // Constellation: always update if IQ bytes were available
        if (canExtractIQ && !iBuf.empty()) {
            const size_t keep           = static_cast<size_t>(config.fftSize);
            auto         trimAppendSnap = [&](std::vector<float> &dst, const std::vector<float> &src) {
                dst.insert(dst.end(), src.begin(), src.end());
                if (dst.size() > keep) dst.erase(dst.begin(), dst.end() - static_cast<std::ptrdiff_t>(keep));
            };
            trimAppendSnap(snapshot_.iSamples, iBuf);
            trimAppendSnap(snapshot_.qSamples, qBuf);
        }
    }
}
