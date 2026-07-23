#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class SampleFormat {
    UInt8     = 0,
    Int16LE   = 1,
    Float32LE = 2,
};

struct StreamConfig {
    std::string  filePath;                    // 재생(서버) 모드: 읽을 .bin 파일
    std::string  ip             = "0.0.0.0";  // 서버 모드: bind IP / 수신 모드: 접속할 서버 IP
    uint16_t     port           = 9000;       // 서버 모드: listen 포트 / 수신 모드: 접속 포트
    int          chunkBytes     = 4096;
    int          fftSize        = 1024;
    float        sampleRateHz   = 48000.0f;
    int          sendIntervalMs = 10;
    bool         loop           = true;
    SampleFormat sampleFormat   = SampleFormat::Int16LE;
    int          channels       = 1;  // 총 채널 수 (1=mono, 2=stereo/IQ)
    int          channelIndex   = 0;  // 사용할 채널 인덱스 (0-based)
    int          frequencyIndex = 0;  // IQ heterodyne offset (shifts spectrum center)
    // true면 payload 스트림에 2048 payload마다 16바이트 HMFT 헤더가 삽입되어
    // 있다고 보고, magic("HMFT")을 스캔해 헤더를 벗겨낸 뒤 payload만 파싱한다.
    bool hmftHeader = false;
};

inline size_t BytesPerScalarSample(SampleFormat sampleFormat) {
    switch (sampleFormat) {
        case SampleFormat::UInt8:
            return 1;
        case SampleFormat::Int16LE:
            return 2;
        case SampleFormat::Float32LE:
            return 4;
    }
    return 1;
}

inline size_t MinChunkBytesForFft(const StreamConfig &config) {
    const size_t fftSize  = static_cast<size_t>(config.fftSize > 0 ? config.fftSize : 1);
    const size_t channels = static_cast<size_t>(config.channels > 0 ? config.channels : 1);
    return fftSize * channels * BytesPerScalarSample(config.sampleFormat);
}

struct StreamSnapshot {
    bool               streamRunning = false;
    bool               captureActive = false;
    bool               connected     = false;
    bool               listening     = false;
    uint64_t           bytesSent     = 0;  // 서버: 전송 / 수신: 수신 바이트 (모드별 의미)
    uint64_t           packetsSent   = 0;  // 서버: 전송 / 수신: 수신 패킷 (모드별 의미)
    uint64_t           fftFrameCount = 0;
    std::string        status        = "Idle";
    std::string        error;
    std::vector<float> frequencies;
    std::vector<float> magnitudesDb;
    std::vector<float> iSamples;  // recent I samples (constellation)
    std::vector<float> qSamples;  // recent Q samples (constellation)

    // 최근 파싱한 HMFT 헤더 정보 (config.hmftHeader가 켜진 경우에만 갱신).
    bool     hmftValid     = false;  // 유효한 헤더를 1개 이상 파싱했는지
    uint32_t hmftSeq       = 0;      // 마지막 프레임의 Sequence Counter
    int      hmftBwCode    = 0;      // 대역폭 코드 (0=200M,1=100M,2=20M,3=10M,4=5M)
    uint32_t hmftCenterKHz = 0;      // Center 주파수 (kHz)
};

// 재생(BinStreamer)과 실시간 수신(IqReceiver)이 공유하는 베이스 클래스.
// 바이트 -> FFT -> 스펙트럼/constellation 처리, 워커 스레드 수명, 스냅샷을
// 담당. 데이터를 어디서 얻을지(파일+서버 vs 클라이언트 접속)는 파생 클래스의
// Run()이 결정.
class IqStream {
   public:
    IqStream() = default;
    virtual ~IqStream();

    IqStream(const IqStream &)            = delete;
    IqStream &operator=(const IqStream &) = delete;

    void           Stop();
    StreamSnapshot Snapshot() const;
    void           SetFreqOffset(float hz) { freqOffsetHz_.store(hz, std::memory_order_relaxed); }

    // 수신 payload를 지정 파일에 기록 시작. 성공 시 true.
    bool StartCapture(const std::string &path, std::string &error);
    // 캡처 중지 및 파일 flush/close.
    void StopCapture();

   protected:
    // 캡처가 활성화된 경우 payload 바이트를 파일에 기록 (스레드 안전).
    void WriteCapture(const uint8_t *data, size_t n);

    // 워커 스레드 시작 전 스냅샷/버퍼 초기화 후 Run()을 새 스레드에서 실행.
    void StartWorker(const StreamConfig &config);
    // 수신한 바이트를 파싱하여 스펙트럼/constellation 스냅샷을 갱신.
    void AppendSamples(const std::vector<uint8_t> &bytes, const StreamConfig &config);
    void SetStatus(const std::string &status, const std::string &error = {});

    virtual void Run(StreamConfig config) = 0;

    mutable std::mutex   mutex_;
    std::thread          worker_;
    std::atomic_bool     stopRequested_ = false;
    std::atomic<float>   freqOffsetHz_{0.0f};
    StreamSnapshot       snapshot_;
    std::vector<float>   sampleBuffer_;    // real-only fallback (mono)
    std::vector<float>   iSampleBuffer_;   // I channel accumulator (IQ mode)
    std::vector<float>   qSampleBuffer_;   // Q channel accumulator (IQ mode)
    std::vector<uint8_t> remainderBytes_;  // chunk 경계 미완성 바이트 carry-over

    // 캡처: STX/ETX 프레임(payload) 1개를 파일 1개로 분리 저장.
    // 캡처 시작 시 디렉터리를 만들고, WriteCapture 호출마다 인덱스 파일을 생성한다.
    mutable std::mutex captureMutex_;       // 캡처 상태 보호 (워커/UI 스레드 공유)
    bool               capturing_ = false;  // 캡처 활성 여부
    std::string        captureDir_;         // payload 파일을 저장할 디렉터리
    uint64_t           captureIndex_ = 0;   // payload 파일 인덱스 (1부터 증가)
};
