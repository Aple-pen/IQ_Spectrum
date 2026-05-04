#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
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
    std::string filePath;
    std::string ip = "0.0.0.0";
    uint16_t port = 9000;
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
    uint64_t bytesSent = 0;
    uint64_t packetsSent = 0;
    uint64_t fftFrameCount = 0;
    std::string status = "Idle";
    std::string error;
    std::vector<float> frequencies;
    std::vector<float> magnitudesDb;
};

class BinStreamer {
public:
    BinStreamer();
    ~BinStreamer();

    BinStreamer(const BinStreamer&) = delete;
    BinStreamer& operator=(const BinStreamer&) = delete;

    bool Start(const StreamConfig& config, std::string& error);
    void Stop();
    StreamSnapshot Snapshot() const;

private:
    void Run(StreamConfig config);
    void AppendSamples(const std::vector<uint8_t>& bytes, const StreamConfig& config);
    void SetStatus(const std::string& status, const std::string& error = {});

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stopRequested_ = false;
    StreamSnapshot snapshot_;
    std::vector<float> sampleBuffer_;
    std::vector<uint8_t> remainderBytes_; // chunk 경계 미완성 바이트 carry-over
};