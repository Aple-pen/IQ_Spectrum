#pragma once

#include "IqStream.h"

#include <string>

// 실시간 수신(클라이언트) 모드: 지정한 TCP 서버에 접속해 IQ 바이트를 수신하고
// 실시간으로 스펙트럼/constellation을 그린다. 연결이 끊기면 자동 재접속을 시도한다.
class IqReceiver : public IqStream {
public:
    bool Start(const StreamConfig& config, std::string& error);

private:
    void Run(StreamConfig config) override;
};
