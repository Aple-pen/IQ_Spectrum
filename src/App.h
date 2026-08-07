#pragma once

#include "BinStreamer.h"
#include "IqReceiver.h"
#include "WbViewerReceiver.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class App {
public:
  void Render();
  void LoadSettings(const char *path = "settings.ini");
  void SaveSettings(const char *path = "settings.ini") const;

private:
  enum class Mode { Playback = 0, Receive = 1, Wideband = 2 };

  // MSG-001 Zone Entry (사용자 편집용). 실제 전송은 big-endian 직렬화.
  struct WbZone {
    int startFreqKHz = 2400000; // 400000 ~ 800000000
    int stopFreqKHz = 2424000;  // start < stop, (stop-start) % bwKHz == 0
    int bwCode = 2;             // 0~4 (0:200 1:100 2:24 3:12 4:6 MHz)
  };

  StreamConfig BuildConfig() const;
  IqStream &Active();
  void BrowseFile();
  // Zone 설정 편집 창(MSG-001). 메인 창과 분리된 별도 ImGui 윈도우.
  void RenderZoneConfig();
  // (stop - start) 가 bw 의 배수가 아니면 stop 을 "가장 가까운" 배수 지점으로
  // 옮긴다. bw 를 바꾸거나 start/stop 편집을 마쳤을 때 호출한다.
  static void SnapZoneStop(WbZone &zone);
  // Zone 설정 export/import (뷰어 전용 포맷, *.zone).
  // settings.ini 와 같은 key=value 텍스트라 손으로 고치기 쉽다:
  //   version=1
  //   queueCount=4
  //   frameCount=12
  //   zone=<start_kHz>,<stop_kHz>,<bwCode>     (zone 당 한 줄)
  // '#' 주석과 빈 줄은 무시하고, 모르는 key 는 건너뛴다(전방 호환).
  bool SaveZones(const char *path, std::string &error) const;
  bool LoadZones(const char *path, std::string &error);
  // 파일 열기/저장 대화상자. 선택 시 zoneFilePath_ 를 채우고 true.
  bool BrowseZoneFile(bool save);
  // 현재 Zone UI 값을 검증한다. 실패 시 false + error(사유).
  bool ValidateZones(std::string &error) const;
  // 검증 후 MSG-001(Zone Configuration) 바이트를 big-endian 으로 직렬화한다.
  bool BuildZoneConfigMsg(std::vector<uint8_t> &out, std::string &error) const;
  void RenderControls(const StreamSnapshot &snapshot);
  void RenderSpectrum(const StreamSnapshot &snapshot, float width);
  // 스펙트럼 라인 1개를 그린다. [i0, i0+count) 구간만 그리며(광대역 3단 분할에
  // 사용), y축 한계는 호출부에서 모든 단이 공유하도록 계산해 전달한다.
  void RenderSpectrumPlot(const float *freqs, const float *mags,
                          const char *plotId, int i0, int count, double yMin,
                          double yMax, bool broadband, float height);
  // 스펙트로그램 히트맵 1개를 그린다. 전체 표시 버퍼(spectrogramDisp_)에서 열
  // 구간 [colBegin, colBegin+colCount)만 잘라내 그리므로, 광대역 3단 분할 시 각
  // 스펙트럼 단 바로 아래에 같은 주파수 구간의 스펙트로그램을 배치할 수 있다.
  void RenderSpectrogramPlot(const char *plotId, int dsColBegin, int dsColCount,
                             double xMin, double xMax, float height,
                             float scaleMin, float scaleMax, bool broadband);
  void RenderConstellation(const StreamSnapshot &snapshot);

  int mode_ = static_cast<int>(Mode::Playback);
  std::array<char, 260> filePath_{};
  std::array<char, 64> ip_{'0', '.', '0', '.', '0', '.', '0', '\0'};
  int port_ = 9000;
  std::array<char, 64> serverIp_{'1', '2', '7', '.', '0',
                                 '.', '0', '.', '1', '\0'};
  int serverPort_ = 9000;
  // 광대역 WB Viewer(ZeroMQ/WBSG) 접속 정보. 접속 IP는 serverIp_ 재사용.
  int wbPort_ = 5557;
  std::array<char, 32> wbTopic_{'W', 'B', '\0'};
  int wbLinesPerCycle_ = 80; // WB: 사이클당 스펙트로그램 행 수 (0=auto=실제 dwell)
  // Zone Configuration(MSG-001) 요청값. Start 시 wb_scann REP(cmd 포트)로 전송.
  int wbCmdPort_ = 5558;      // 제어 채널(REQ→REP) 포트
  int wbQueueCount_ = 4;      // Step 당 큐 수 (4의 배수)
  int wbFrameCount_ = 12;     // 큐 당 프레임 수 (1~65535)
  std::vector<WbZone> wbZones_{WbZone{}}; // 최소 1개 zone
  bool showZoneConfig_ = false;           // Zone 설정 창 표시 여부
  std::array<char, 260> zoneFilePath_{};  // 마지막으로 export/import 한 경로
  std::string zoneFileStatus_;            // 최근 저장/로드 결과 메시지
  int fftSize_ = 1024;
  int chunkBytes_ = 4096;
  int sendIntervalMs_ = 10;
  float sampleRateHz_ = 48000.0f;
  bool loop_ = true;
  int sampleFormatIndex_ = static_cast<int>(SampleFormat::Int16LE);
  int frequencyIndex_ = 0;
  int channels_ = 1;
  int channelIndex_ = 0;
  bool hmftHeader_ = false; // payload에 2048당 16B HMFT 헤더 삽입 여부
  bool showDemo_ = false;
  bool chartDark_ = true;
  bool yAxisAuto_ = true;
  float yAxisMin_ = -160.0f;
  float yAxisMax_ = 10.0f;
  bool spectrumTiers_ = false; // 광대역 스펙트럼을 여러 단으로 나눠 표시
  int spectrumTierCount_ = 3;  // 분할할 단(段) 수
  bool showSpectrogram_ = false;
  int spectrogramRows_ = 240;      // number of time rows to keep (표시 상한과 동일)
  int spectrogramHeightPct_ = 60;  // 단 블록에서 스펙트로그램이 차지할 높이 비율(%)
  bool showConstellation_ = false;
  int constellationPoints_ = 1024; // number of IQ points to display
  float freqOffsetHz_ = 0.0f; // IQ heterodyne offset (shifts spectrum center)
  std::vector<float> spectrogramBuf_; // ring buffer [row * bins + bin]
  std::vector<float>
      spectrogramDisp_; // 다운샘플된 표시 행렬 [row * dsCols + col], 새 프레임에만 갱신
  std::vector<float>
      spectrogramTile_;     // 단(段)별 열 구간 추출용 버퍼 (재사용)
  int spectrogramHead_ = 0; // index of oldest row
  int spectrogramFill_ = 0; // number of valid rows written
  int spectrogramBins_ = 0; // current bin count (detect reset)
  int spectrogramDsCols_ = 0;      // 다운샘플 후 표시 열 수 (<= 원본 bins)
  int spectrogramDsRows_ = 0;      // 다운샘플 후 표시 행 수 (<= History rows)
  bool spectrogramDirty_ = false;  // 표시 행렬 재구성 필요 여부(새 프레임/리셋)

  // WB 스펙트럼(상단 라인) 애니메이션 상태: 한 사이클의 dwell 행을 매 프레임
  // 1행씩 순차적으로 그린다. 새 사이클이 오면 그 사이클의 0행부터 다시 시작.
  std::shared_ptr<const std::vector<float>> wbSpectrumBlock_;
  uint64_t wbSpectrumCycle_ = 0; // 현재 애니메이션 중인 사이클 (fftFrameCount)
  int wbSpectrumRow_ = 0;        // 다음에 그릴 행 인덱스
  int wbSpectrumBlockRows_ = 0;  // 현재 블록의 행 수
  double wbSpectrumYMin_ = -160.0; // 이 사이클 전체 기준 y 오토스케일 (고정 스케일)
  double wbSpectrumYMax_ = 10.0;
  uint64_t lastFftFrameCount_ = 0;
  int lastMode_ =
      static_cast<int>(Mode::Playback); // 모드 전환 시 스펙트로그램 리셋용
  std::string lastError_;
  BinStreamer streamer_;        // 재생(서버) 모드
  IqReceiver receiver_;         // 실시간 수신(클라이언트) 모드
  WbViewerReceiver wbReceiver_; // 광대역 WB Viewer(WBSG/ZeroMQ) 모드
};