#pragma once

#include <cstdint>
#include <string>

// 실시간 수신(클라이언트) 모드용 TCP 클라이언트.
// 지정한 서버 IP:port에 접속하여 바이트 스트림을 수신한다.
class TcpReceiver {
public:
    TcpReceiver();
    ~TcpReceiver();

    TcpReceiver(const TcpReceiver&) = delete;
    TcpReceiver& operator=(const TcpReceiver&) = delete;

    // 서버에 접속. 성공 시 true. timeoutMs 동안 연결을 기다린다.
    bool Connect(const std::string& ip, uint16_t port, int timeoutMs, std::string& error);
    // 최대 maxBytes 바이트 수신. 반환값: >0 수신 바이트, 0 연결 종료, <0 오류(error 설정).
    // timeoutMs 동안 데이터가 없으면 0을 반환하지 않고 다시 기다린다(블로킹). -2 = 타임아웃(중단 확인용).
    int Recv(uint8_t* buffer, int maxBytes, int timeoutMs, std::string& error);
    void Close();
    bool IsConnected() const;

private:
    uintptr_t socket_;
};
