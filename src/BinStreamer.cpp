#include "BinStreamer.h"

#include <chrono>
#include <fstream>
#include <thread>

#include "Fft.h"
#include "TcpSender.h"

bool BinStreamer::Start(const StreamConfig &config, std::string &error) {
    if (config.filePath.empty()) {
        error = "Select a .bin file first";
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
    if (config.port == 0) {
        error = "Port must be 1..65535";
        return false;
    }

    StartWorker(config);
    return true;
}

void BinStreamer::Run(StreamConfig config) {
    TcpSender   sender;
    std::string error;
    if (!sender.StartListening(config.ip, config.port, error)) {
        SetStatus("Listen failed", error);
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.streamRunning = false;
        snapshot_.connected     = false;
        snapshot_.listening     = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.listening = true;
        snapshot_.connected = false;
        snapshot_.status    = "Listening (spectrum active)";
    }

    const size_t         defaultChunkBytes = static_cast<size_t>(std::max(config.chunkBytes, 1));
    size_t               activeChunkBytes  = defaultChunkBytes;
    std::vector<uint8_t> buffer(activeChunkBytes);
    while (!stopRequested_) {
        std::ifstream file(config.filePath, std::ios::binary);
        if (!file) {
            SetStatus("File open failed", config.filePath);
            break;
        }

        while (!stopRequested_ && file) {
            if (!sender.IsConnected()) {
                if (sender.WaitForClient(1, error)) {
                    int         requestedChunkBytes = config.chunkBytes;
                    std::string handshakeError;
                    const bool  hasChunkRequest =
                        sender.TryRecvChunkRequest(20, 250, requestedChunkBytes, handshakeError);
                    if (!handshakeError.empty()) {
                        sender.DisconnectClient();
                        std::lock_guard<std::mutex> lock(mutex_);
                        snapshot_.connected = false;
                        snapshot_.status    = "Client handshake failed";
                        snapshot_.error     = handshakeError;
                        activeChunkBytes    = defaultChunkBytes;
                        buffer.resize(activeChunkBytes);
                        continue;
                    }

                    activeChunkBytes =
                        hasChunkRequest ? static_cast<size_t>(std::max(requestedChunkBytes, 1)) : defaultChunkBytes;
                    buffer.resize(activeChunkBytes);

                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_.connected = true;
                    snapshot_.error.clear();
                    snapshot_.status =
                        hasChunkRequest ? "Client connected, negotiated streaming" : "Client connected, streaming";
                } else if (!error.empty()) {
                    SetStatus("Accept failed", error);
                    stopRequested_ = true;
                    break;
                }
            }

            file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize readCount = file.gcount();
            if (readCount <= 0) {
                break;
            }

            std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + readCount);
            AppendSamples(packet, config);

            if (sender.IsConnected()) {
                if (!sender.SendAll(packet, error)) {
                    sender.DisconnectClient();
                    activeChunkBytes = defaultChunkBytes;
                    buffer.resize(activeChunkBytes);
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_.connected = false;
                    snapshot_.status    = "Client disconnected, spectrum-only";
                    error.clear();
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_.bytesSent += static_cast<uint64_t>(packet.size());
                    snapshot_.packetsSent += 1;
                }
            }

            if (config.sendIntervalMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.sendIntervalMs));
            }
        }

        if (!config.loop) {
            break;
        }
        // 파일 루프 시 나머지 바이트 리셋 (새 파일은 바이트 0부터 시작)
        std::lock_guard<std::mutex> loopLock(mutex_);
        remainderBytes_.clear();
    }

    sender.StopListening();
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.streamRunning = false;
    snapshot_.connected     = false;
    snapshot_.listening     = false;
    if (snapshot_.error.empty()) {
        snapshot_.status = "Completed";
    }
}
