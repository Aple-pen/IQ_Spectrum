#pragma once

#include "IqStream.h"

#include <cstddef>
#include <cstdint>
#include <string>

// 광대역 WB Viewer 수신 모드 (ICD-0001).
// wb_scanner(Jetson)가 ZeroMQ PUB 로 발행하는 "1스캔 사이클 스냅샷"(WBSG v2)을
// ZeroMQ SUB 로 구독해, 밴드 전체를 이어붙인 스펙트럼으로 그린다.
//
// 기존 HMFT-over-TCP(IqReceiver)와는 전송/포맷이 완전히 다른 독립 경로다:
//   - 전송: ZeroMQ SUB connect, topic 구독, recv_multipart 마지막 프레임이 payload
//   - 포맷: WBSG v2 (헤더 72B + slice entry 40B*N + 슬라이스별 seq/data), little-endian
//   - 한 메시지 = 한 사이클(모든 슬라이스). 결손(cycleIndex 불연속/complete==0) 정상.
class WbViewerReceiver : public IqStream {
public:
  bool Start(const StreamConfig &config, std::string &error);

private:
  void Run(StreamConfig config) override;

  // 수신한 WBSG payload 한 개(=한 사이클)를 파싱해 스냅샷(밴드 스펙트럼)을 갱신.
  // 형식 오류면 조용히 무시한다(그 메시지만 폐기).
  void HandleCycle(const uint8_t *data, size_t len);

  uint32_t lastCycleIndex_ = 0;
  bool haveLastCycle_ = false;
  uint64_t dropCount_ = 0;  // cycleIndex 불연속으로 추정한 드롭 사이클 수
  uint64_t cycleCount_ = 0; // 파싱 성공한 사이클 수
  int wbLinesPerCycle_ = 0; // 사이클당 표시 행 수 (0=auto, config 에서 복사)
};
