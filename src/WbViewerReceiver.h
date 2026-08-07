#pragma once

#include "IqStream.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

  // MSG-001(Zone Configuration, magic "WCMD") 요청을 wb_scann 의 제어 채널
  // (ZeroMQ REP, 기본 tcp://<ip>:5558)로 보내고 16B WRSP 응답을 받는다.
  // IF-06 제어 채널: 우리 뷰어가 REQ(connect), wb_scann 이 REP(bind).
  // request 는 이미 직렬화된 MSG-001 바이트(big-endian). 응답 status 는
  // respStatus 로 돌려준다(0 = 정상 가정). 통신 실패 시 false + error.
  static bool RequestZoneConfig(const std::string &ip, uint16_t port,
                                const std::vector<uint8_t> &request,
                                std::string &error, uint32_t &respStatus,
                                std::string &respMagic);

private:
  void Run(StreamConfig config) override;

  // 수신한 WBSG payload 한 개(=한 사이클)를 파싱해 스냅샷(밴드 스펙트럼)을 갱신.
  // 형식 오류면 그 메시지만 폐기하되, 사유를 상태줄에 남긴다(NoteDiscard).
  void HandleCycle(const uint8_t *data, size_t len);
  // 폐기 사유를 카운트하고 상태줄에 노출한다. 조용히 버리면 "데이터가 아예 안 온다"
  // 와 구분이 안 되어 디버깅이 불가능해진다.
  void NoteDiscard(const std::string &reason);

  uint32_t lastCycleIndex_ = 0;
  bool haveLastCycle_ = false;
  uint64_t dropCount_ = 0;  // cycleIndex 불연속으로 추정한 드롭 사이클 수
  uint64_t cycleCount_ = 0;   // 파싱 성공한 사이클 수
  uint64_t discardCount_ = 0; // 형식 문제로 폐기한 메시지 수

  // 균일 주파수 격자를 사이클마다 다시 잡으면, 부분 사이클(슬라이스 결손)에서
  // 범위·간격이 흔들려 x축이 튀고 스펙트로그램 링버퍼도 리셋된다. 그래서 플랜
  // 기준으로 한 번 잡아 고정(latch)하고, 플랜이 바뀔 때만 다시 잡는다.
  double gridBandLoKHz_ = 0.0;
  double gridBandHiKHz_ = 0.0;
  double gridMinStepKHz_ = 0.0; // 지금까지 본 가장 촘촘한 zone 해상도
  double gridStepKHz_ = 0.0;
  size_t gridCols_ = 0;
  int wbLinesPerCycle_ = 0; // 사이클당 표시 행 수 (0=auto, config 에서 복사)
};
