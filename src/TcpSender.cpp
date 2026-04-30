#include "TcpSender.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

namespace {
constexpr uintptr_t InvalidSocketValue = static_cast<uintptr_t>(INVALID_SOCKET);

std::string LastSocketError(const char* operation) {
    return std::string(operation) + " failed, WSA error " + std::to_string(WSAGetLastError());
}
}

TcpSender::TcpSender()
    : listenSocket_(InvalidSocketValue)
    , clientSocket_(InvalidSocketValue) {
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
}

TcpSender::~TcpSender() {
    StopListening();
    WSACleanup();
}

bool TcpSender::StartListening(const std::string& bindIp, uint16_t port, std::string& error) {
    StopListening();

    SOCKET listenSock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        error = LastSocketError("socket");
        return false;
    }

    BOOL reuse = TRUE;
    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) == SOCKET_ERROR) {
        error = LastSocketError("setsockopt(SO_REUSEADDR)");
        closesocket(listenSock);
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    const std::string ip = bindIp.empty() ? "0.0.0.0" : bindIp;
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        closesocket(listenSock);
        error = "Invalid bind IPv4 address";
        return false;
    }

    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        error = LastSocketError("bind");
        closesocket(listenSock);
        return false;
    }

    if (::listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        error = LastSocketError("listen");
        closesocket(listenSock);
        return false;
    }

    listenSocket_ = static_cast<uintptr_t>(listenSock);
    return true;
}

bool TcpSender::WaitForClient(int timeoutMs, std::string& error) {
    if (listenSocket_ == InvalidSocketValue) {
        error = "Server is not listening";
        return false;
    }
    if (IsConnected()) {
        return true;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(static_cast<SOCKET>(listenSocket_), &readSet);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
    if (ready == 0) {
        return false;
    }
    if (ready == SOCKET_ERROR) {
        error = LastSocketError("select");
        return false;
    }

    SOCKET client = accept(static_cast<SOCKET>(listenSocket_), nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        error = LastSocketError("accept");
        return false;
    }

    clientSocket_ = static_cast<uintptr_t>(client);
    return true;
}

bool TcpSender::SendAll(const std::vector<uint8_t>& bytes, std::string& error) {
    if (!IsConnected()) {
        error = "Socket is not connected";
        return false;
    }

    const char* data = reinterpret_cast<const char*>(bytes.data());
    int remaining = static_cast<int>(bytes.size());
    while (remaining > 0) {
        const int sent = ::send(static_cast<SOCKET>(clientSocket_), data, remaining, 0);
        if (sent == SOCKET_ERROR) {
            error = LastSocketError("send");
            return false;
        }
        if (sent == 0) {
            error = "send returned 0, peer closed connection";
            return false;
        }
        data += sent;
        remaining -= sent;
    }
    return true;
}

void TcpSender::DisconnectClient() {
    if (clientSocket_ != InvalidSocketValue) {
        closesocket(static_cast<SOCKET>(clientSocket_));
        clientSocket_ = InvalidSocketValue;
    }
}

void TcpSender::StopListening() {
    DisconnectClient();
    if (listenSocket_ != InvalidSocketValue) {
        closesocket(static_cast<SOCKET>(listenSocket_));
        listenSocket_ = InvalidSocketValue;
    }
}

bool TcpSender::IsConnected() const {
    return clientSocket_ != InvalidSocketValue;
}