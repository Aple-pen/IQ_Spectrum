#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "IqStream.h"

class TcpReceiver;

// 실시간 수신(클라이언트) 모드: 지정한 TCP 서버에 접속해 IQ 바이트를 수신하고
// 실시간으로 스펙트럼/constellation을 그린다. 연결이 끊기면 자동 재접속을 시도한다.
class IqReceiver : public IqStream {
   public:
    bool Start(const StreamConfig& config, std::string& error);

   private:
    void Run(StreamConfig config) override;

    // 정확히 n 바이트를 수신할 때까지 블로킹. 성공 시 true.
    // 중단 요청/연결 종료/오류 시 false (오류 메시지는 error에 설정, 중단 시 비움).
    bool RecvExact(TcpReceiver& client, uint8_t* dst, size_t n, std::string& error);
};
