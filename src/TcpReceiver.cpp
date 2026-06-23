#include "TcpReceiver.h"

#include <WS2tcpip.h>
#include <WinSock2.h>

#include <climits>

namespace {
constexpr uintptr_t InvalidSocketValue = static_cast<uintptr_t>(INVALID_SOCKET);

std::string LastSocketError(const char *operation) {
  return std::string(operation) + " failed, WSA error " +
         std::to_string(WSAGetLastError());
}
} // namespace

TcpReceiver::TcpReceiver() : socket_(InvalidSocketValue) {
  WSADATA data{};
  WSAStartup(MAKEWORD(2, 2), &data);
}

TcpReceiver::~TcpReceiver() {
  Close();
  WSACleanup();
}

bool TcpReceiver::Connect(const std::string &ip, uint16_t port, int timeoutMs,
                          std::string &error) {
  Close();

  SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    error = LastSocketError("socket");
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  const std::string target = ip.empty() ? "127.0.0.1" : ip;
  if (inet_pton(AF_INET, target.c_str(), &address.sin_addr) != 1) {
    closesocket(sock);
    error = "Invalid server IPv4 address";
    return false;
  }

  // 논블로킹 모드로 전환하여 connect 타임아웃을 select로 제어
  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);

  const int rc =
      ::connect(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  if (rc == SOCKET_ERROR) {
    const int wsaErr = WSAGetLastError();
    if (wsaErr != WSAEWOULDBLOCK) {
      error = LastSocketError("connect");
      closesocket(sock);
      return false;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);
    fd_set errSet;
    FD_ZERO(&errSet);
    FD_SET(sock, &errSet);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    const int ready = select(0, nullptr, &writeSet, &errSet, &timeout);
    if (ready <= 0) {
      error = (ready == 0) ? "connect timed out" : LastSocketError("select");
      closesocket(sock);
      return false;
    }
    if (FD_ISSET(sock, &errSet)) {
      error = "connect refused";
      closesocket(sock);
      return false;
    }
  }

  // 다시 블로킹 모드로 복귀
  u_long blocking = 0;
  ioctlsocket(sock, FIONBIO, &blocking);

  socket_ = static_cast<uintptr_t>(sock);
  return true;
}

bool TcpReceiver::SendAll(const uint8_t *bytes, size_t count,
                          std::string &error) {
  if (!IsConnected()) {
    error = "Not connected";
    return false;
  }

  const char *data = reinterpret_cast<const char *>(bytes);
  size_t remaining = count;
  while (remaining > 0) {
    const int want = static_cast<int>(
        remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : remaining);
    const int sent = ::send(static_cast<SOCKET>(socket_), data, want, 0);
    if (sent == SOCKET_ERROR) {
      error = LastSocketError("send");
      return false;
    }
    if (sent == 0) {
      error = "send returned 0, peer closed connection";
      return false;
    }
    data += sent;
    remaining -= static_cast<size_t>(sent);
  }
  return true;
}

int TcpReceiver::Recv(uint8_t *buffer, int maxBytes, int timeoutMs,
                      std::string &error) {
  if (!IsConnected()) {
    error = "Not connected";
    return -1;
  }

  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(static_cast<SOCKET>(socket_), &readSet);

  timeval timeout{};
  timeout.tv_sec = timeoutMs / 1000;
  timeout.tv_usec = (timeoutMs % 1000) * 1000;

  const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
  if (ready == 0) {
    return -2; // 타임아웃: 호출자가 stop 여부 확인 후 재시도
  }
  if (ready == SOCKET_ERROR) {
    error = LastSocketError("select");
    return -1;
  }

  const int received = ::recv(static_cast<SOCKET>(socket_),
                              reinterpret_cast<char *>(buffer), maxBytes, 0);
  if (received == SOCKET_ERROR) {
    error = LastSocketError("recv");
    return -1;
  }
  return received; // 0 == 서버가 연결 종료
}

void TcpReceiver::Close() {
  if (socket_ != InvalidSocketValue) {
    closesocket(static_cast<SOCKET>(socket_));
    socket_ = InvalidSocketValue;
  }
}

bool TcpReceiver::IsConnected() const { return socket_ != InvalidSocketValue; }
