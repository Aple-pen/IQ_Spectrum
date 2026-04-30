#pragma once

#include <cstdint>
#include <string>
#include <vector>

class TcpSender {
public:
    TcpSender();
    ~TcpSender();

    TcpSender(const TcpSender&) = delete;
    TcpSender& operator=(const TcpSender&) = delete;

    bool StartListening(const std::string& bindIp, uint16_t port, std::string& error);
    bool WaitForClient(int timeoutMs, std::string& error);
    bool SendAll(const std::vector<uint8_t>& bytes, std::string& error);
    void DisconnectClient();
    void StopListening();
    bool IsConnected() const;

private:
    uintptr_t listenSocket_;
    uintptr_t clientSocket_;
};