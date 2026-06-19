#include "IqReceiver.h"

#include "Fft.h"
#include "TcpReceiver.h"

#include <chrono>
#include <thread>

bool IqReceiver::Start(const StreamConfig& config, std::string& error) {
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

void IqReceiver::Run(StreamConfig config) {
    std::vector<uint8_t> buffer(static_cast<size_t>(config.chunkBytes));

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

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.connected = true;
            snapshot_.listening = false;
            snapshot_.status = "Connected, receiving";
            // 재접속 시 carry-over 바이트 리셋 (새 스트림 시작)
            remainderBytes_.clear();
        }

        while (!stopRequested_) {
            const int n = client.Recv(buffer.data(), static_cast<int>(buffer.size()), 200, error);
            if (n == -2) {
                continue; // 타임아웃: stop 여부 재확인
            }
            if (n <= 0) {
                // 0 = 서버 종료, <0 = 오류
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.connected = false;
                snapshot_.status = (n == 0) ? "Server closed connection" : "Receive error";
                if (n < 0) snapshot_.error = error;
                break;
            }

            std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + n);
            AppendSamples(packet, config);

            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.bytesSent += static_cast<uint64_t>(n);
            snapshot_.packetsSent += 1;
        }

        client.Close();

        if (!config.loop) {
            break;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.running = false;
    snapshot_.connected = false;
    snapshot_.listening = false;
    if (snapshot_.error.empty()) {
        snapshot_.status = "Stopped";
    }
}
