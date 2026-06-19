#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class SampleFormat {
    UInt8 = 0,
    Int16LE = 1,
    Float32LE = 2,
};

struct StreamConfig {
    std::string filePath;       // 재생(서버) 모드: 읽을 .bin 파일
    std::string ip = "0.0.0.0"; // 서버 모드: bind IP / 수신 모드: 접속할 서버 IP
    uint16_t port = 9000;       // 서버 모드: listen 포트 / 수신 모드: 접속 포트
    int chunkBytes = 4096;
    int fftSize = 1024;
    float sampleRateHz = 48000.0f;
    int sendIntervalMs = 10;
    bool loop = true;
    SampleFormat sampleFormat = SampleFormat::Int16LE;
    int channels = 1;       // 총 채널 수 (1=mono, 2=stereo/IQ)
    int channelIndex = 0;   // 사용할 채널 인덱스 (0-based)
};

struct StreamSnapshot {
    bool running = false;
    bool connected = false;
    bool listening = false;
    uint64_t bytesSent = 0;     // 서버: 전송 / 수신: 수신 바이트 (모드별 의미)
    uint64_t packetsSent = 0;   // 서버: 전송 / 수신: 수신 패킷 (모드별 의미)
    uint64_t fftFrameCount = 0;
    std::string status = "Idle";
    std::string error;
    std::vector<float> frequencies;
    std::vector<float> magnitudesDb;
    std::vector<float> iSamples;   // recent I samples (constellation)
    std::vector<float> qSamples;   // recent Q samples (constellation)
};

// 재생(BinStreamer)과 실시간 수신(IqReceiver)이 공유하는 베이스 클래스.
// 바이트 -> FFT -> 스펙트럼/constellation 처리, 워커 스레드 수명, 스냅샷을 담당.
// 데이터를 어디서 얻을지(파일+서버 vs 클라이언트 접속)는 파생 클래스의 Run()이 결정.
class IqStream {
public:
    IqStream() = default;
    virtual ~IqStream();

    IqStream(const IqStream&) = delete;
    IqStream& operator=(const IqStream&) = delete;

    void Stop();
    StreamSnapshot Snapshot() const;
    void SetFreqOffset(float hz) { freqOffsetHz_.store(hz, std::memory_order_relaxed); }

protected:
    // 워커 스레드 시작 전 스냅샷/버퍼 초기화 후 Run()을 새 스레드에서 실행.
    void StartWorker(const StreamConfig& config);
    // 수신한 바이트를 파싱하여 스펙트럼/constellation 스냅샷을 갱신.
    void AppendSamples(const std::vector<uint8_t>& bytes, const StreamConfig& config);
    void SetStatus(const std::string& status, const std::string& error = {});

    virtual void Run(StreamConfig config) = 0;

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stopRequested_ = false;
    std::atomic<float> freqOffsetHz_{ 0.0f };
    StreamSnapshot snapshot_;
    std::vector<float> sampleBuffer_;     // real-only fallback (mono)
    std::vector<float> iSampleBuffer_;    // I channel accumulator (IQ mode)
    std::vector<float> qSampleBuffer_;    // Q channel accumulator (IQ mode)
    std::vector<uint8_t> remainderBytes_; // chunk 경계 미완성 바이트 carry-over
};
