#pragma once

#include "BinStreamer.h"
#include "IqReceiver.h"
#include "WbViewerReceiver.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class App {
public:
  void Render();
  void LoadSettings(const char *path = "settings.ini");
  void SaveSettings(const char *path = "settings.ini") const;

private:
  enum class Mode { Playback = 0, Receive = 1, Wideband = 2 };

  StreamConfig BuildConfig() const;
  IqStream &Active();
  void BrowseFile();
  void RenderControls(const StreamSnapshot &snapshot);
  void RenderSpectrum(const StreamSnapshot &snapshot, float width);
  // 스펙트럼 라인 1개를 그린다. [i0, i0+count) 구간만 그리며(광대역 3단 분할에
  // 사용), y축 한계는 호출부에서 모든 단이 공유하도록 계산해 전달한다.
  void RenderSpectrumPlot(const StreamSnapshot &snapshot, const char *plotId,
                          int i0, int count, double yMin, double yMax,
                          bool broadband, float height);
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
  int wbLinesPerCycle_ = 0; // WB: 사이클당 스펙트로그램 행 수 (0=auto=실제 dwell)
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
  int spectrogramRows_ = 200;      // number of time rows to keep
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
  uint64_t lastFftFrameCount_ = 0;
  int lastMode_ =
      static_cast<int>(Mode::Playback); // 모드 전환 시 스펙트로그램 리셋용
  std::string lastError_;
  BinStreamer streamer_;        // 재생(서버) 모드
  IqReceiver receiver_;         // 실시간 수신(클라이언트) 모드
  WbViewerReceiver wbReceiver_; // 광대역 WB Viewer(WBSG/ZeroMQ) 모드
};