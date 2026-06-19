#pragma once

#include "IqStream.h"

#include <string>

// 재생(서버) 모드: .bin 파일을 읽어 TCP 서버로 listen 하고,
// 접속한 클라이언트에 IQ 바이트를 전송하면서 동시에 스펙트럼을 그린다.
class BinStreamer : public IqStream {
public:
    bool Start(const StreamConfig& config, std::string& error);

private:
    void Run(StreamConfig config) override;
};
