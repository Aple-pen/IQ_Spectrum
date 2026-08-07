#include "App.h"

#include "Fft.h"

#include <Windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <implot.h>

#include "constants.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace port {
enum class FrequencyValues {
  FREQ_915_MHZ = 23000,
  FREQ_433_MHZ = 23001,
  FREQ_5_8_GHZ_LOW = 23002,
  FREQ_5_8_GHZ_MID = 23003,
  FREQ_5_8_GHZ_HIGH = 23004,
  FREQ_2_4_GHZ_LOW = 23005,
  FREQ_2_4_GHZ_HIGH = 23006,
};
}
namespace {
// 스펙트로그램 히트맵의 최대 표시 열 수. PlotHeatmap은 셀당 사각형을 하나씩
// 그리므로, 광대역에서 bins(=슬라이스수*2048)가 커져도 이 값으로 열을
// 다운샘플(max-pooling)해 렌더 비용을 상한으로 묶는다. 화면 폭보다 크게 그려도
// 어차피 보이지 않으므로 시각적 손실은 거의 없다.
constexpr int kMaxSpectrogramCols = 1024;
// 스펙트로그램 히트맵의 최대 표시 행 수(무조건 상한). History rows 를 더 크게
// 잡아 오래 저장해도 화면에는 최대 이 행수까지만 그린다. 각 사이클을 80행으로
// decimation 하므로 240 = 최근 3사이클 분량. PlotHeatmap 은 셀당 사각형을
// 하나씩 그리므로 렌더 비용은 셀 수(행×열)에 비례한다.
constexpr int kMaxSpectrogramRows = 240;

int PushImGuiTheme(bool dark) {
  if (dark) {
    // Dark: Charcoal Blue + Amber accent
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.059f, 0.067f, 0.090f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImVec4(0.082f, 0.102f, 0.149f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.886f, 0.910f, 0.957f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,
                          ImVec4(0.420f, 0.475f, 0.600f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(0.165f, 0.208f, 0.314f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImVec4(0.110f, 0.141f, 0.220f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.149f, 0.188f, 0.282f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.188f, 0.235f, 0.353f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,
                          ImVec4(0.059f, 0.067f, 0.090f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImVec4(0.082f, 0.102f, 0.149f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.145f, 0.200f, 0.337f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.192f, 0.282f, 0.471f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.239f, 0.349f, 0.580f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,
                          ImVec4(0.145f, 0.200f, 0.337f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImVec4(0.192f, 0.282f, 0.471f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                          ImVec4(0.239f, 0.349f, 0.580f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,
                          ImVec4(1.000f, 0.792f, 0.188f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                          ImVec4(1.000f, 0.792f, 0.188f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                          ImVec4(1.000f, 0.878f, 0.400f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator,
                          ImVec4(0.165f, 0.208f, 0.314f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered,
                          ImVec4(1.000f, 0.792f, 0.188f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,
                          ImVec4(0.059f, 0.067f, 0.090f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                          ImVec4(0.165f, 0.208f, 0.314f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                          ImVec4(0.239f, 0.349f, 0.580f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,
                          ImVec4(0.082f, 0.102f, 0.149f, 1.0f));
  } else {
    // Light: Soft White + Steel Blue accent
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.933f, 0.945f, 0.961f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImVec4(1.000f, 1.000f, 1.000f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.102f, 0.137f, 0.251f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,
                          ImVec4(0.478f, 0.545f, 0.667f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(0.769f, 0.804f, 0.847f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImVec4(0.878f, 0.906f, 0.937f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.800f, 0.839f, 0.894f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.718f, 0.769f, 0.847f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,
                          ImVec4(0.933f, 0.945f, 0.961f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImVec4(0.816f, 0.855f, 0.906f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.820f, 0.890f, 0.969f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.700f, 0.816f, 0.941f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.573f, 0.733f, 0.910f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,
                          ImVec4(0.749f, 0.816f, 0.906f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImVec4(0.604f, 0.718f, 0.847f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                          ImVec4(0.467f, 0.627f, 0.800f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,
                          ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                          ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                          ImVec4(0.114f, 0.357f, 0.608f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator,
                          ImVec4(0.769f, 0.804f, 0.847f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered,
                          ImVec4(0.169f, 0.424f, 0.690f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,
                          ImVec4(0.933f, 0.945f, 0.961f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                          ImVec4(0.769f, 0.804f, 0.847f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                          ImVec4(0.467f, 0.627f, 0.800f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,
                          ImVec4(1.000f, 1.000f, 1.000f, 1.0f));
  }
  return 25;
}

const char *FrequencyName(int index) {
  static const char *kNames[] = {"915 MHz",     "433 MHz",      "5.8 GHz_Low",
                                 "5.8 GHz_Mid", "5.8 GHz_High", "2.4 GHz Low",
                                 "2.4 GHz High"};
  if (index < 0 || index >= 7) {
    return "unknown";
  }
  return kNames[index];
}

// 캡처 파일명 생성: "yyyymmdd hhmmss _ <frequency>.bin"
// (Windows 파일명에 ':' 사용 불가하여 시:분:초 구분자는 생략)
std::string MakeCaptureFileName(int frequencyIndex) {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d %02d%02d%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);
  return std::string(stamp) + " _ " + FrequencyName(frequencyIndex) + ".bin";
}

const char *SampleFormatName(int index) {
  switch (static_cast<SampleFormat>(index)) {
  case SampleFormat::UInt8:
    return "uint8 centered";
  case SampleFormat::Int16LE:
    return "int16 little-endian";
  case SampleFormat::Float32LE:
    return "float32 little-endian";
  default:
    return "unknown";
  }
}
} // namespace

void App::LoadSettings(const char *path) {
  std::ifstream f(path);
  if (!f)
    return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '[')
      continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    if (key == "filePath") {
      strncpy_s(filePath_.data(), filePath_.size(), val.c_str(),
                filePath_.size() - 1);
    } else if (key == "ip") {
      strncpy_s(ip_.data(), ip_.size(), val.c_str(), ip_.size() - 1);
    } else if (key == "port") {
      port_ = std::stoi(val);
    } else if (key == "mode") {
      mode_ = std::stoi(val);
      lastMode_ = mode_;
    } else if (key == "serverIp") {
      strncpy_s(serverIp_.data(), serverIp_.size(), val.c_str(),
                serverIp_.size() - 1);
    } else if (key == "serverPort") {
      serverPort_ = std::stoi(val);
    } else if (key == "wbPort") {
      wbPort_ = std::stoi(val);
    } else if (key == "wbTopic") {
      strncpy_s(wbTopic_.data(), wbTopic_.size(), val.c_str(),
                wbTopic_.size() - 1);
    } else if (key == "wbLinesPerCycle") {
      wbLinesPerCycle_ = std::stoi(val);
    } else if (key == "wbCmdPort") {
      wbCmdPort_ = std::stoi(val);
    } else if (key == "wbQueueCount") {
      wbQueueCount_ = std::stoi(val);
    } else if (key == "wbFrameCount") {
      wbFrameCount_ = std::stoi(val);
    } else if (key == "wbZoneFile") {
      strncpy_s(zoneFilePath_.data(), zoneFilePath_.size(), val.c_str(),
                zoneFilePath_.size() - 1);
    } else if (key == "wbZones") {
      // 형식: "start,stop,bw;start,stop,bw;..."
      std::vector<WbZone> parsed;
      std::stringstream zs(val);
      std::string zone;
      while (std::getline(zs, zone, ';')) {
        if (zone.find_first_not_of(" \t\r\n") == std::string::npos)
          continue;
        std::stringstream fs(zone);
        std::string a, b, c;
        if (std::getline(fs, a, ',') && std::getline(fs, b, ',') &&
            std::getline(fs, c, ',')) {
          try {
            WbZone z;
            z.startFreqKHz = std::stoi(a);
            z.stopFreqKHz = std::stoi(b);
            z.bwCode = std::stoi(c);
            parsed.push_back(z);
          } catch (...) {
            // 손상된 항목은 건너뜀
          }
        }
      }
      if (!parsed.empty()) {
        wbZones_ = std::move(parsed);
      }
    } else if (key == "fftSize") {
      const int v = std::stoi(val);
      // Snap to nearest valid power-of-two >= 256
      int snapped = 256;
      for (int s : {256, 512, 1024, 2048, 4096, 8192, 16384})
        if (s <= v)
          snapped = s;
      fftSize_ = snapped;
    } else if (key == "chunkBytes") {
      chunkBytes_ = std::stoi(val);
    } else if (key == "sendIntervalMs") {
      sendIntervalMs_ = std::stoi(val);
    } else if (key == "sampleRateHz") {
      sampleRateHz_ = std::stof(val);
    } else if (key == "loop") {
      loop_ = val == "1";
    } else if (key == "sampleFormatIndex") {
      sampleFormatIndex_ = std::stoi(val);
    } else if (key == "channels") {
      channels_ = std::stoi(val);
    } else if (key == "channelIndex") {
      channelIndex_ = std::stoi(val);
    } else if (key == "hmftHeader") {
      hmftHeader_ = val == "1";
    } else if (key == "chartDark") {
      chartDark_ = val == "1";
    } else if (key == "yAxisAuto") {
      yAxisAuto_ = val == "1";
    } else if (key == "yAxisMin") {
      yAxisMin_ = std::stof(val);
    } else if (key == "yAxisMax") {
      yAxisMax_ = std::stof(val);
    } else if (key == "spectrumTiers") {
      spectrumTiers_ = val == "1";
    } else if (key == "spectrumTierCount") {
      spectrumTierCount_ = std::stoi(val);
    } else if (key == "showHistogram") {
      showSpectrogram_ = val == "1";
    } else if (key == "histogramBins") {
      spectrogramRows_ = std::stoi(val);
    } else if (key == "showSpectrogram") {
      showSpectrogram_ = val == "1";
    } else if (key == "spectrogramRows") {
      spectrogramRows_ = std::stoi(val);
    } else if (key == "spectrogramHeightPct") {
      spectrogramHeightPct_ = std::stoi(val);
    } else if (key == "freqOffsetHz") {
      freqOffsetHz_ = std::stof(val);
    } else if (key == "frequencyIndex") {
      frequencyIndex_ = std::stoi(val);
    }
  }
}

void App::SaveSettings(const char *path) const {
  std::ofstream f(path);
  if (!f)
    return;
  f << "[Settings]\n";
  f << "mode=" << mode_ << '\n';
  f << "filePath=" << filePath_.data() << '\n';
  f << "ip=" << ip_.data() << '\n';
  f << "port=" << port_ << '\n';
  f << "serverIp=" << serverIp_.data() << '\n';
  f << "serverPort=" << serverPort_ << '\n';
  f << "wbPort=" << wbPort_ << '\n';
  f << "wbTopic=" << wbTopic_.data() << '\n';
  f << "wbLinesPerCycle=" << wbLinesPerCycle_ << '\n';
  f << "wbCmdPort=" << wbCmdPort_ << '\n';
  f << "wbQueueCount=" << wbQueueCount_ << '\n';
  f << "wbFrameCount=" << wbFrameCount_ << '\n';
  f << "wbZoneFile=" << zoneFilePath_.data() << '\n';
  f << "wbZones=";
  for (size_t i = 0; i < wbZones_.size(); ++i) {
    if (i)
      f << ';';
    f << wbZones_[i].startFreqKHz << ',' << wbZones_[i].stopFreqKHz << ','
      << wbZones_[i].bwCode;
  }
  f << '\n';
  f << "fftSize=" << fftSize_ << '\n';
  f << "chunkBytes=" << chunkBytes_ << '\n';
  f << "sendIntervalMs=" << sendIntervalMs_ << '\n';
  f << "sampleRateHz=" << sampleRateHz_ << '\n';
  f << "loop=" << (loop_ ? 1 : 0) << '\n';
  f << "sampleFormatIndex=" << sampleFormatIndex_ << '\n';
  f << "channels=" << channels_ << '\n';
  f << "channelIndex=" << channelIndex_ << '\n';
  f << "hmftHeader=" << (hmftHeader_ ? 1 : 0) << '\n';
  f << "chartDark=" << (chartDark_ ? 1 : 0) << '\n';
  f << "yAxisAuto=" << (yAxisAuto_ ? 1 : 0) << '\n';
  f << "yAxisMin=" << yAxisMin_ << '\n';
  f << "yAxisMax=" << yAxisMax_ << '\n';
  f << "spectrumTiers=" << (spectrumTiers_ ? 1 : 0) << '\n';
  f << "spectrumTierCount=" << spectrumTierCount_ << '\n';
  f << "showHistogram=" << (showSpectrogram_ ? 1 : 0) << '\n';
  f << "histogramBins=" << spectrogramRows_ << '\n';
  f << "showSpectrogram=" << (showSpectrogram_ ? 1 : 0) << '\n';
  f << "spectrogramRows=" << spectrogramRows_ << '\n';
  f << "spectrogramHeightPct=" << spectrogramHeightPct_ << '\n';
  f << "freqOffsetHz=" << freqOffsetHz_ << '\n';
  f << "frequencyIndex=" << frequencyIndex_ << '\n';
}

IqStream &App::Active() {
  if (mode_ == static_cast<int>(Mode::Wideband)) {
    return static_cast<IqStream &>(wbReceiver_);
  }
  if (mode_ == static_cast<int>(Mode::Receive)) {
    return static_cast<IqStream &>(receiver_);
  }
  return static_cast<IqStream &>(streamer_);
}

void App::Render() {
  // 모드 전환 시 스펙트로그램 누적 버퍼 리셋 (다른 컴포넌트의 frame count와
  // 섞이지 않도록)
  if (mode_ != lastMode_) {
    lastMode_ = mode_;
    spectrogramBins_ = 0;
    spectrogramHead_ = 0;
    spectrogramFill_ = 0;
    lastFftFrameCount_ = 0;
    // WB 스펙트럼 애니메이션 상태도 리셋
    wbSpectrumBlock_.reset();
    wbSpectrumCycle_ = 0;
    wbSpectrumRow_ = 0;
    wbSpectrumBlockRows_ = 0;
  }

  const StreamSnapshot snapshot = Active().Snapshot();

  const int themeColors = PushImGuiTheme(chartDark_);
  // 멀티뷰포트가 켜져 있으면 창 좌표가 데스크톱 절대 좌표다. (0,0) + DisplaySize
  // 로 두면 메인 창이 뷰포트 밖으로 나간 것으로 판정되어 자기 자신이 별도 OS
  // 창으로 떨어져 나간다. 메인 뷰포트에 명시적으로 고정한다.
  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(mainViewport->WorkSize, ImGuiCond_Always);
  ImGui::SetNextWindowViewport(mainViewport->ID);
  ImGui::Begin("Bin TCP Spectrum", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  RenderControls(snapshot);
  ImGui::SameLine();
  RenderSpectrum(snapshot, 0.0f);

  ImGui::End();

  // Zone 설정 창은 메인 창과 분리된 별도 윈도우. 테마 색을 공유하도록 pop 전에.
  if (showZoneConfig_ && mode_ == static_cast<int>(Mode::Wideband)) {
    RenderZoneConfig();
  }

  ImGui::PopStyleColor(themeColors);

  if (showConstellation_) {
    RenderConstellation(snapshot);
  }

  if (showDemo_) {
    ImPlot::ShowDemoWindow(&showDemo_);
  }
}

StreamConfig App::BuildConfig() const {
  StreamConfig config;
  config.filePath = filePath_.data();
  const bool receiveMode = (mode_ == static_cast<int>(Mode::Receive));
  const bool wideband = (mode_ == static_cast<int>(Mode::Wideband));
  if (wideband) {
    // 광대역 WB Viewer: 접속 IP는 serverIp_ 재사용, 포트/토픽은 전용.
    config.ip = serverIp_.data();
    config.port = static_cast<uint16_t>(std::clamp(wbPort_, 1, 65535));
    config.wbTopic = wbTopic_.data();
    config.wbLinesPerCycle = wbLinesPerCycle_;
  } else if (receiveMode) {
    config.ip = serverIp_.data();
    config.port = static_cast<uint16_t>(std::clamp(serverPort_, 1, 65535));
  } else {
    config.ip = ip_.data();
    config.port = static_cast<uint16_t>(std::clamp(port_, 1, 65535));
  }
  config.fftSize = fftSize_;
  config.sendIntervalMs = sendIntervalMs_;
  config.sampleRateHz = sampleRateHz_;
  config.loop = loop_;
  config.sampleFormat = static_cast<SampleFormat>(sampleFormatIndex_);
  config.channels = std::max(1, channels_);
  config.channelIndex =
      std::max(0, std::min(channelIndex_, config.channels - 1));
  config.frequencyIndex = frequencyIndex_;
  config.hmftHeader = hmftHeader_;
  if (receiveMode) {
    const size_t autoChunkBytes = MinChunkBytesForFft(config);
    config.chunkBytes =
        autoChunkBytes > static_cast<size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(autoChunkBytes);
  } else {
    config.chunkBytes = chunkBytes_;
  }
  return config;
}

bool App::SaveZones(const char *path, std::string &error) const {
  if (path == nullptr || path[0] == '\0') {
    error = "no file path";
    return false;
  }
  std::ofstream f(path);
  if (!f) {
    error = std::string("cannot open for write: ") + path;
    return false;
  }
  f << "# iq_spectrum zone configuration\n";
  f << "# zone=<start_kHz>,<stop_kHz>,<bwCode>  "
       "(bw 0=200 1=100 2=24 3=12 4=6 MHz)\n";
  f << "version=1\n";
  f << "queueCount=" << wbQueueCount_ << '\n';
  f << "frameCount=" << wbFrameCount_ << '\n';
  for (const WbZone &z : wbZones_) {
    f << "zone=" << z.startFreqKHz << ',' << z.stopFreqKHz << ',' << z.bwCode
      << '\n';
  }
  if (!f) {
    error = "write failed";
    return false;
  }
  return true;
}

bool App::LoadZones(const char *path, std::string &error) {
  if (path == nullptr || path[0] == '\0') {
    error = "no file path";
    return false;
  }
  std::ifstream f(path);
  if (!f) {
    error = std::string("cannot open: ") + path;
    return false;
  }

  auto trim = [](const std::string &s) -> std::string {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
      return std::string();
    }
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
  };
  auto parseInt = [&](const std::string &cell, int &out) -> bool {
    const std::string t = trim(cell);
    if (t.empty()) {
      return false;
    }
    try {
      size_t pos = 0;
      const long long v = std::stoll(t, &pos);
      if (pos != t.size() || v < 0 ||
          v > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
      }
      out = static_cast<int>(v);
      return true;
    } catch (...) {
      return false;
    }
  };

  // 전부 성공했을 때만 반영하도록 로컬에 먼저 담는다(로드 실패 시 기존 설정 보존).
  std::vector<WbZone> zones;
  int queueCount = wbQueueCount_;
  int frameCount = wbFrameCount_;
  std::string line;
  size_t lineNo = 0;

  while (std::getline(f, line)) {
    ++lineNo;
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == '[') {
      continue; // 빈 줄 / 주석 / 섹션 헤더
    }
    const size_t eq = t.find('=');
    if (eq == std::string::npos) {
      error = "line " + std::to_string(lineNo) + ": expected key=value";
      return false;
    }
    const std::string key = trim(t.substr(0, eq));
    const std::string val = trim(t.substr(eq + 1));

    if (key == "zone") {
      std::stringstream ss(val);
      std::string a, b, c;
      if (!std::getline(ss, a, ',') || !std::getline(ss, b, ',') ||
          !std::getline(ss, c, ',')) {
        error = "line " + std::to_string(lineNo) +
                ": zone needs start,stop,bw";
        return false;
      }
      WbZone z;
      if (!parseInt(a, z.startFreqKHz) || !parseInt(b, z.stopFreqKHz) ||
          !parseInt(c, z.bwCode)) {
        error = "line " + std::to_string(lineNo) + ": bad zone value";
        return false;
      }
      if (z.bwCode < 0 || z.bwCode > 4) {
        error = "line " + std::to_string(lineNo) + ": bw must be 0..4";
        return false;
      }
      zones.push_back(z);
    } else if (key == "queueCount") {
      if (!parseInt(val, queueCount)) {
        error = "line " + std::to_string(lineNo) + ": bad queueCount";
        return false;
      }
    } else if (key == "frameCount") {
      if (!parseInt(val, frameCount)) {
        error = "line " + std::to_string(lineNo) + ": bad frameCount";
        return false;
      }
    }
    // 그 밖의 key(version 포함)는 무시 — 전방 호환.
  }

  if (zones.empty()) {
    error = "no zone= entries found";
    return false;
  }
  if (zones.size() > 64) {
    error = "too many zones (" + std::to_string(zones.size()) + " > 64)";
    return false;
  }

  wbZones_ = std::move(zones);
  wbQueueCount_ = queueCount;
  wbFrameCount_ = frameCount;
  return true;
}

bool App::BrowseZoneFile(bool save) {
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = "Zone config (*.zone)\0*.zone\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = zoneFilePath_.data();
  ofn.nMaxFile = static_cast<DWORD>(zoneFilePath_.size());
  ofn.lpstrDefExt = "zone";
  if (save) {
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) == TRUE;
  }
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameA(&ofn) == TRUE;
}

void App::SnapZoneStop(WbZone &zone) {
  const int bw = constants::WB::BW_KHz[std::clamp(zone.bwCode, 0, 4)];
  const long long start = zone.startFreqKHz;
  long long span = static_cast<long long>(zone.stopFreqKHz) - start;
  // 이미 배수면 그대로 둔다(사용자 입력 보존).
  if (span > 0 && (span % bw) == 0) {
    return;
  }
  // 가장 가까운 배수로 반올림. 0 이하로 떨어지면 최소 1스텝.
  long long steps = (span + bw / 2) / bw;
  if (steps < 1) {
    steps = 1;
  }
  long long stop = start + steps * bw;
  // 상한(800000000 kHz)을 넘으면 스텝을 줄여 맞춘다.
  while (stop > 800000000LL && steps > 1) {
    --steps;
    stop = start + steps * bw;
  }
  zone.stopFreqKHz = static_cast<int>(stop);
}

bool App::ValidateZones(std::string &error) const {
  const int n = static_cast<int>(wbZones_.size());
  if (n < 1 || n > 64) {
    error = "num_zones must be 1..64 (now " + std::to_string(n) + ")";
    return false;
  }
  if (wbQueueCount_ <= 0 || (wbQueueCount_ % 4) != 0) {
    error = "queue_count must be a positive multiple of 4";
    return false;
  }
  if (wbFrameCount_ < 1 || wbFrameCount_ > 65535) {
    error = "frame_count must be 1..65535";
    return false;
  }
  for (int i = 0; i < n; ++i) {
    const WbZone &z = wbZones_[static_cast<size_t>(i)];
    const std::string zi = "zone " + std::to_string(i) + ": ";
    if (z.bwCode < 0 || z.bwCode > 4) {
      error = zi + "bw code must be 0..4";
      return false;
    }
    if (z.startFreqKHz < 400000 || z.startFreqKHz > 800000000 ||
        z.stopFreqKHz < 400000 || z.stopFreqKHz > 800000000) {
      error = zi + "freq out of range (400000..800000000 kHz)";
      return false;
    }
    if (z.startFreqKHz >= z.stopFreqKHz) {
      error = zi + "start_freq must be < stop_freq";
      return false;
    }
    const long long bw = constants::WB::BW_KHz[z.bwCode];
    if ((static_cast<long long>(z.stopFreqKHz - z.startFreqKHz) % bw) != 0) {
      error = zi + "(stop-start) must be a multiple of bw (" +
              std::to_string(bw) + " kHz)";
      return false;
    }
  }
  return true;
}

bool App::BuildZoneConfigMsg(std::vector<uint8_t> &out,
                             std::string &error) const {
  if (!ValidateZones(error)) {
    return false;
  }
  auto putBE = [](std::vector<uint8_t> &b, uint32_t v) {
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
  };
  out.clear();
  out.reserve(16 + 16 * wbZones_.size());
  // Header 16B: magic "WCMD" + num_zones + queue_cnt + frame_count (big-endian)
  out.push_back(0x57); // W
  out.push_back(0x43); // C
  out.push_back(0x4D); // M
  out.push_back(0x44); // D
  putBE(out, static_cast<uint32_t>(wbZones_.size()));
  putBE(out, static_cast<uint32_t>(wbQueueCount_));
  putBE(out, static_cast<uint32_t>(wbFrameCount_));
  // Zone Entry 16B × N: start_freq + stop_freq + bw + reserved (big-endian)
  for (const WbZone &z : wbZones_) {
    putBE(out, static_cast<uint32_t>(z.startFreqKHz));
    putBE(out, static_cast<uint32_t>(z.stopFreqKHz));
    putBE(out, static_cast<uint32_t>(z.bwCode));
    putBE(out, 0u);
  }
  return true;
}

void App::BrowseFile() {
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = "Binary files (*.bin)\0*.bin\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = filePath_.data();
  ofn.nMaxFile = static_cast<DWORD>(filePath_.size());
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn) == TRUE) {
    lastError_.clear();
  }
}

void App::RenderControls(const StreamSnapshot &snapshot) {
  ImGui::BeginChild("Controls", ImVec2(360, 0), true);
  const ImVec4 accent = chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f)
                                   : ImVec4(0.169f, 0.424f, 0.690f, 1.0f);
  const ImVec4 warnColor = chartDark_ ? ImVec4(1.000f, 0.576f, 0.196f, 1.0f)
                                      : ImVec4(0.851f, 0.400f, 0.051f, 1.0f);
  const ImVec4 infoColor = chartDark_ ? ImVec4(0.353f, 0.953f, 0.647f, 1.0f)
                                      : ImVec4(0.102f, 0.549f, 0.200f, 1.0f);
  const ImVec4 errColor = chartDark_ ? ImVec4(1.000f, 0.420f, 0.420f, 1.0f)
                                     : ImVec4(0.800f, 0.100f, 0.100f, 1.0f);

  // Compute label column width dynamically from the widest label
  const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
  const float maxLabelW = std::max({
                              ImGui::CalcTextSize("Bind IP").x,
                              ImGui::CalcTextSize("Listen Port").x,
                              ImGui::CalcTextSize("Chunk bytes").x,
                              ImGui::CalcTextSize("Interval ms").x,
                              ImGui::CalcTextSize("FFT size").x,
                              ImGui::CalcTextSize("RBW (Hz)").x,
                              ImGui::CalcTextSize("Sample rate Hz").x,
                              ImGui::CalcTextSize("Sample format").x,
                              ImGui::CalcTextSize("Channels").x,
                              ImGui::CalcTextSize("Channel index").x,
                          }) +
                          innerSpacing * 2.0f;
  ImGui::PushItemWidth(-maxLabelW);

  // 캡처 중에는 Start/Stop · Capture 버튼을 제외한 모든 컨트롤을 잠금.
  ImGui::BeginDisabled(snapshot.captureActive);

  // 모드 선택: 재생(서버) / 실시간 수신(클라이언트) / 광대역 WB Viewer.
  // 실행 중에는 잠금.
  ImGui::TextColored(accent, "Mode");
  ImGui::BeginDisabled(snapshot.streamRunning);
  ImGui::RadioButton("Playback (Server)", &mode_,
                     static_cast<int>(Mode::Playback));
  ImGui::SameLine();
  ImGui::RadioButton("Receive (Client)", &mode_,
                     static_cast<int>(Mode::Receive));
  ImGui::RadioButton("Wideband (WB Viewer)", &mode_,
                     static_cast<int>(Mode::Wideband));
  ImGui::EndDisabled();

  const bool receiveMode = (mode_ == static_cast<int>(Mode::Receive));
  const bool wideband = (mode_ == static_cast<int>(Mode::Wideband));

  ImGui::Separator();
  if (wideband) {
    // 광대역 WB Viewer(WBSG/ZeroMQ) 모드: 스캐너 IP/포트/토픽에 SUB 접속.
    ImGui::TextColored(accent, "WB Viewer (ZeroMQ SUB)");
    ImGui::InputText("Scanner IP", serverIp_.data(), serverIp_.size());
    ImGui::InputInt("Port", &wbPort_);
    if (wbPort_ < 1)
      wbPort_ = 1;
    if (wbPort_ > 65535)
      wbPort_ = 65535;
    ImGui::InputText("Topic", wbTopic_.data(), wbTopic_.size());
    ImGui::TextColored(infoColor, "tcp://%s:%d  topic=\"%s\"", serverIp_.data(),
                       wbPort_, wbTopic_.data());
    ImGui::Checkbox("Auto-reconnect", &loop_);
    // 사이클당 스펙트로그램 행 수. 0 = 사이클의 실제 dwell 행 수 그대로(압축
    // 없음).
    ImGui::InputInt("Lines/cycle (0=auto)", &wbLinesPerCycle_);
    if (wbLinesPerCycle_ < 0)
      wbLinesPerCycle_ = 0;
    if (wbLinesPerCycle_ > 4096)
      wbLinesPerCycle_ = 4096;
    ImGui::TextColored(infoColor, "WBSG v2 cycle snapshot (self-describing)");

    // --- Zone Configuration (MSG-001) ---
    // Start 시 이 값으로 MSG-001 을 만들어 wb_scann REP(Cmd port)에 REQ 로
    // 보낸다. 실제 편집은 별도 창(RenderZoneConfig)에서 한다.
    ImGui::Separator();
    ImGui::TextColored(accent, "Zone Config (MSG-001, on Start)");
    if (ImGui::Button("Zone settings...", ImVec2(-1, 0))) {
      showZoneConfig_ = true;
    }
    std::string zerr;
    if (ValidateZones(zerr)) {
      ImGui::TextColored(infoColor, "%d zone(s), cmd port %d",
                         static_cast<int>(wbZones_.size()), wbCmdPort_);
    } else {
      ImGui::TextColored(warnColor, "%s", zerr.c_str());
    }
  } else if (!receiveMode) {
    // 재생(서버) 모드: 파일 + bind IP/listen 포트
    ImGui::TextColored(accent, "Source");
    ImGui::SetNextItemWidth(-76);
    ImGui::InputText("##file", filePath_.data(), filePath_.size(),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
      BrowseFile();
    }

    ImGui::Separator();
    ImGui::TextColored(accent, "TCP Server");
    ImGui::InputText("Bind IP", ip_.data(), ip_.size());
    ImGui::InputInt("Listen Port", &port_);

    ImGui::Separator();
    ImGui::TextColored(accent, "Stream");
    ImGui::InputInt("Chunk bytes", &chunkBytes_);
    ImGui::InputInt("Interval ms", &sendIntervalMs_);
    ImGui::Checkbox("Loop file", &loop_);
  } else {
    // 실시간 수신(클라이언트) 모드: 접속할 서버 IP/포트
    ImGui::TextColored(accent, "TCP Client");
    ImGui::InputText("Server IP", serverIp_.data(), serverIp_.size());
    static const port::FrequencyValues FrequencyValues[] = {
        port::FrequencyValues::FREQ_915_MHZ,
        port::FrequencyValues::FREQ_433_MHZ,
        port::FrequencyValues::FREQ_5_8_GHZ_LOW,
        port::FrequencyValues::FREQ_5_8_GHZ_MID,
        port::FrequencyValues::FREQ_5_8_GHZ_HIGH,
        port::FrequencyValues::FREQ_2_4_GHZ_LOW,
        port::FrequencyValues::FREQ_2_4_GHZ_HIGH};

    if (ImGui::BeginCombo("Frequency", FrequencyName(frequencyIndex_))) {
      for (int index = 0; index < 7; ++index) {
        const bool selected = frequencyIndex_ == index;
        if (ImGui::Selectable(FrequencyName(index), selected)) {
          frequencyIndex_ = index;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    serverPort_ = static_cast<int>(FrequencyValues[frequencyIndex_]);
    // ImGui::InputInt("Server Port", &serverPort_);

    ImGui::Separator();
    ImGui::TextColored(accent, "Stream");
    const StreamConfig autoConfig = BuildConfig();
    const size_t autoChunkBytes = MinChunkBytesForFft(autoConfig);
    ImGui::Text("Auto recv bytes: %zu", autoChunkBytes);
    if (sampleRateHz_ > 0.0f) {
      const float fftWindowMs =
          1000.0f * static_cast<float>(fftSize_) / sampleRateHz_;
      ImGui::TextColored(infoColor, "1 FFT window: %.3f ms", fftWindowMs);
    }
    ImGui::Checkbox("Auto-reconnect", &loop_);
  }

  ImGui::Separator();
  ImGui::TextColored(accent, "Y Axis");
  ImGui::Checkbox("Auto scale", &yAxisAuto_);
  if (!yAxisAuto_) {
    ImGui::InputFloat("Y min (dB)", &yAxisMin_);
    ImGui::InputFloat("Y max (dB)", &yAxisMax_);
  }

  ImGui::Separator();
  ImGui::TextColored(accent, "Wideband");
  ImGui::Checkbox("Split spectrum into tiers", &spectrumTiers_);
  if (spectrumTiers_) {
    ImGui::InputInt("Tiers", &spectrumTierCount_);
    if (spectrumTierCount_ < 1)
      spectrumTierCount_ = 1;
    if (spectrumTierCount_ > 8)
      spectrumTierCount_ = 8;
    if (!wideband && !snapshot.hmftValid) {
      ImGui::TextColored(warnColor, "Best with HMFT wideband stream");
    }
  }

  // 아래 FFT/샘플/채널/HMFT 섹션은 IQ·HMFT 경로 전용이라 WB Viewer 모드에서는
  // 숨김 (WBSG 는 자기기술 포맷이라 이 설정들이 필요 없다).
  if (!wideband) {
    ImGui::Separator();
    ImGui::TextColored(accent, "FFT");
    {
      static const int kFftSizes[] = {256, 512, 1024, 2048, 4096, 8192, 16384};
      static const char *kFftLabels[] = {"256",  "512",  "1024", "2048",
                                         "4096", "8192", "16384"};
      constexpr int kFftCount = 7;

      // Helper: snap fs/rbw to nearest valid FFT size >= 256
      auto rbwToFftSize = [&](float rbw) -> int {
        if (rbw <= 0.0f || sampleRateHz_ <= 0.0f)
          return fftSize_;
        const float ideal = sampleRateHz_ / rbw;
        int best = kFftSizes[0];
        float bestDist = std::abs(ideal - static_cast<float>(best));
        for (int i = 1; i < kFftCount; ++i) {
          const float d = std::abs(ideal - static_cast<float>(kFftSizes[i]));
          if (d < bestDist) {
            bestDist = d;
            best = kFftSizes[i];
          }
        }
        return best;
      };

      int curIdx = 0;
      for (int i = 0; i < kFftCount; ++i)
        if (kFftSizes[i] == fftSize_) {
          curIdx = i;
          break;
        }

      if (ImGui::BeginCombo("FFT size", kFftLabels[curIdx])) {
        for (int i = 0; i < kFftCount; ++i) {
          const bool selected = (i == curIdx);
          if (ImGui::Selectable(kFftLabels[i], selected))
            fftSize_ = kFftSizes[i];
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      // RBW display/edit: RBW = fs / N
      float rbw = (sampleRateHz_ > 0.0f && fftSize_ > 0)
                      ? sampleRateHz_ / static_cast<float>(fftSize_)
                      : 0.0f;
      if (ImGui::InputFloat("RBW (Hz)", &rbw, 0.0f, 0.0f, "%.2f")) {
        fftSize_ = rbwToFftSize(rbw);
      }
    }
    ImGui::InputFloat("Sample rate Hz", &sampleRateHz_);
    if (ImGui::BeginCombo("Sample format",
                          SampleFormatName(sampleFormatIndex_))) {
      for (int index = 0; index < 3; ++index) {
        const bool selected = sampleFormatIndex_ == index;
        if (ImGui::Selectable(SampleFormatName(index), selected)) {
          sampleFormatIndex_ = index;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::InputInt("Channels", &channels_);
    if (channels_ < 1) {
      channels_ = 1;
    }
    ImGui::InputInt("Channel index", &channelIndex_);
    if (channelIndex_ < 0) {
      channelIndex_ = 0;
    }
    if (channelIndex_ >= channels_) {
      channelIndex_ = channels_ - 1;
    }
    if (channels_ > 1) {
      ImGui::TextColored(infoColor, "Multi-ch: using ch %d of %d",
                         channelIndex_, channels_ - 1);
    }

    // HMFT 헤더는 광대역 스캔(1채널) 모드 전용.
    if (channels_ == 1) {
      ImGui::Separator();
      ImGui::TextColored(accent, "Frame Header");
      ImGui::Checkbox("HMFT header (16B/2048)", &hmftHeader_);
      if (hmftHeader_) {
        if (snapshot.hmftValid) {
          // 대역폭 코드 -> MHz (200M=0,100M=1,20M=2,10M=3,5M=4)
          static const char *kBwName[] = {"200M", "100M", "20M", "10M", "5M"};
          const char *bw = (snapshot.hmftBwCode >= 0 && snapshot.hmftBwCode < 5)
                               ? kBwName[snapshot.hmftBwCode]
                               : "?";
          ImGui::TextColored(
              infoColor, "seq %u  BW %s  Center %.3f MHz", snapshot.hmftSeq, bw,
              static_cast<double>(snapshot.hmftCenterKHz) / 1000.0);
        } else {
          ImGui::TextColored(warnColor, "No HMFT magic parsed yet");
        }
      }
    } // channels_ == 1
  } // !wideband (FFT/샘플/채널/HMFT 섹션)

  ImGui::Separator();
  ImGui::TextColored(accent, "Chart Style");
  if (ImGui::Button("Black", ImVec2(80, 24))) {
    chartDark_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("White", ImVec2(80, 24))) {
    chartDark_ = false;
  }

  ImGui::Separator();
  ImGui::TextColored(accent, "Spectrogram");
  ImGui::Checkbox("Show spectrogram", &showSpectrogram_);
  if (showSpectrogram_) {
    // 유지할 시간 행 수. WB 모드는 사이클당 여러 dwell 행(예: 480)이
    // 들어오므로, 한 사이클을 온전히 보려면 이 값을 사이클 행 수(Lines/cycle)
    // 이상으로 둔다.
    ImGui::InputInt("History rows", &spectrogramRows_);
    if (spectrogramRows_ < 10)
      spectrogramRows_ = 10;
    if (spectrogramRows_ > 4096)
      spectrogramRows_ = 4096;
    // 각 단 블록에서 스펙트로그램이 차지하는 세로 비율. 나머지는 스펙트럼 몫.
    ImGui::SliderInt("Spectrogram height %", &spectrogramHeightPct_, 20, 80);
  }

  // Constellation·IQ Freq Offset 도 IQ 경로 전용이라 WB Viewer 모드에서는 숨김.
  if (!wideband) {
    ImGui::Separator();
    ImGui::TextColored(accent, "Constellation");
    ImGui::Checkbox("Show constellation", &showConstellation_);
    if (showConstellation_) {
      ImGui::InputInt("Points", &constellationPoints_);
      if (constellationPoints_ < 64)
        constellationPoints_ = 64;
      if (constellationPoints_ > 8192)
        constellationPoints_ = 8192;
      if (channels_ < 2) {
        ImGui::TextColored(warnColor, "Needs Channels >= 2 (IQ)");
      }
    }

    ImGui::Separator();
    ImGui::TextColored(accent, "IQ Freq Offset");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::DragFloat(
            "##freqOffset", &freqOffsetHz_,
            sampleRateHz_ > 0 ? sampleRateHz_ / static_cast<float>(fftSize_)
                              : 1.0f,
            -sampleRateHz_ * 0.5f, sampleRateHz_ * 0.5f, "%.1f Hz")) {
      Active().SetFreqOffset(freqOffsetHz_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##fo")) {
      freqOffsetHz_ = 0.0f;
      Active().SetFreqOffset(0.0f);
    }
    // keep active component in sync even if value unchanged (e.g. after load)
    Active().SetFreqOffset(freqOffsetHz_);
  }

  // 여기까지가 캡처 중 잠금 대상. 아래 Start/Stop·Capture 버튼은 활성 유지.
  ImGui::EndDisabled();

  ImGui::Separator();
  if (!snapshot.streamRunning) {
    if (ImGui::Button("Start", ImVec2(110, 34))) {
      std::string error;
      bool ok = false;
      if (wideband) {
        // Start 시 먼저 MSG-001(Zone Configuration)을 wb_scann 제어 채널로
        // 보내고, WRSP status 가 정상(0)일 때만 WBSG SUB 수신을 시작한다.
        std::vector<uint8_t> msg;
        std::string zerr;
        if (!BuildZoneConfigMsg(msg, zerr)) {
          error = "Zone config invalid: " + zerr;
        } else {
          uint32_t status = 0;
          std::string magic;
          std::string reqErr;
          if (!WbViewerReceiver::RequestZoneConfig(
                  serverIp_.data(),
                  static_cast<uint16_t>(std::clamp(wbCmdPort_, 1, 65535)), msg,
                  reqErr, status, magic)) {
            error = "Zone config request failed: " + reqErr;
          } else if (status != 0) {
            error = "Zone config rejected (magic=" + magic +
                    " status=" + std::to_string(status) + ")";
          } else {
            ok = wbReceiver_.Start(BuildConfig(), error);
          }
        }
      } else if (receiveMode) {
        ok = receiver_.Start(BuildConfig(), error);
      } else {
        ok = streamer_.Start(BuildConfig(), error);
      }
      if (!ok && error.empty()) {
        // (방어) 이유 없는 실패는 일반 메시지로
        error = "Start failed";
      }
      if (!ok) {
        lastError_ = error;
      }
    }
  } else {
    if (ImGui::Button("Stop", ImVec2(110, 34))) {
      Active().Stop();
    }
  }
  ImGui::SameLine();
  // 캡처는 Receive 모드에서만 동작 (TCP payload 수신 경로)
  ImGui::BeginDisabled(!receiveMode);
  if (!snapshot.captureActive) {
    if (ImGui::Button("Capture", ImVec2(110, 34))) {
      // STX/ETX를 제외한 수신 payload를 파일로 기록 시작
      const std::string path = MakeCaptureFileName(frequencyIndex_);
      std::string error;
      if (!Active().StartCapture(path, error)) {
        lastError_ = error;
      }
    }
  } else {
    if (ImGui::Button("Stop Capture", ImVec2(110, 34))) {
      Active().StopCapture();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(snapshot.captureActive);
  if (ImGui::Button("ImPlot Demo", ImVec2(110, 34))) {
    showDemo_ = !showDemo_;
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  ImGui::Text("Status: %s", snapshot.status.c_str());
  if (!receiveMode && !wideband) {
    ImGui::Text("Listening: %s", snapshot.listening ? "yes" : "no");
  }
  ImGui::Text("Connected: %s", snapshot.connected ? "yes" : "no");
  const bool recvLike = receiveMode || wideband;
  ImGui::Text("%s: %llu",
              wideband ? "Cycles"
                       : (recvLike ? "Recv packets" : "Sent packets"),
              static_cast<unsigned long long>(snapshot.packetsSent));
  ImGui::Text("%s: %llu", recvLike ? "Recv bytes" : "Sent bytes",
              static_cast<unsigned long long>(snapshot.bytesSent));

  if (!snapshot.error.empty()) {
    ImGui::TextColored(errColor, "%s", snapshot.error.c_str());
  } else if (!lastError_.empty()) {
    ImGui::TextColored(errColor, "%s", lastError_.c_str());
  }

  ImGui::PopItemWidth();
  ImGui::EndChild();
}

void App::RenderSpectrum(const StreamSnapshot &snapshot, float width) {
  ImGui::BeginChild("Spectrum", ImVec2(width, 0), true);
  {
    const ImVec4 specAccent = chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f)
                                         : ImVec4(0.169f, 0.424f, 0.690f, 1.0f);
    ImGui::TextColored(specAccent, "Spectrum");
  }
  ImGui::Separator();

  if (chartDark_) {
    ImPlot::PushStyleColor(ImPlotCol_PlotBg,
                           ImVec4(0.031f, 0.035f, 0.055f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg,
                           ImVec4(0.059f, 0.078f, 0.125f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisText,
                           ImVec4(0.478f, 0.561f, 0.710f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                           ImVec4(0.102f, 0.145f, 0.251f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisTick,
                           ImVec4(0.165f, 0.227f, 0.345f, 1.0f));
  } else {
    ImPlot::PushStyleColor(ImPlotCol_PlotBg,
                           ImVec4(0.961f, 0.973f, 1.000f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg,
                           ImVec4(0.910f, 0.937f, 0.973f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisText,
                           ImVec4(0.227f, 0.314f, 0.439f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                           ImVec4(0.753f, 0.816f, 0.878f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisTick,
                           ImVec4(0.478f, 0.604f, 0.733f, 1.0f));
  }

  const float availH = ImGui::GetContentRegionAvail().y;

  // HMFT(광대역 1채널) 모드: x축은 실제 주파수(MHz), y축은 dB가 아닌 원신호 값.
  const bool broadband = snapshot.hmftValid;
  const int specN = static_cast<int>(snapshot.magnitudesDb.size());
  const bool haveData =
      specN > 0 && snapshot.frequencies.size() == snapshot.magnitudesDb.size();

  // --- WB 스펙트럼 애니메이션 ---
  // 한 사이클이 dwell n행을 담고 있으면, 상단 스펙트럼 라인은 그 n행을 매
  // 프레임 1행씩 순차적으로 그린다(라이브 스윕). 다 그리기 전에 다음 사이클이
  // 오면 (fftFrameCount 변화) 그 사이클의 0행부터 다시 시작한다. 스펙트로그램은
  // 별개로 n행 전부 누적된다. 그리는 값 소스: 기본은 magnitudesDb(요약), WB
  // 애니메이션 중이면 현재 행.
  const float *specMags = haveData ? snapshot.magnitudesDb.data() : nullptr;
  bool animating = false;
  if (haveData && snapshot.spectrogramBlock &&
      snapshot.spectrogramBlockRows > 0 &&
      static_cast<int>(snapshot.spectrogramBlock->size()) ==
          snapshot.spectrogramBlockRows * specN) {
    if (snapshot.fftFrameCount != wbSpectrumCycle_) {
      // 새 사이클: 0행부터 다시 시작하고, 이 사이클 전체 기준으로 y 스케일
      // 고정.
      wbSpectrumCycle_ = snapshot.fftFrameCount;
      wbSpectrumBlock_ = snapshot.spectrogramBlock;
      wbSpectrumBlockRows_ = snapshot.spectrogramBlockRows;
      wbSpectrumRow_ = 0;
      double bmin = 0.0;
      double bmax = 0.0;
      bool any = false;
      for (float v : *wbSpectrumBlock_) {
        if (std::isfinite(v)) {
          if (!any) {
            bmin = bmax = v;
            any = true;
          } else {
            bmin = std::min(bmin, static_cast<double>(v));
            bmax = std::max(bmax, static_cast<double>(v));
          }
        }
      }
      const double margin = any ? std::max(6.0, (bmax - bmin) * 0.15) : 6.0;
      wbSpectrumYMin_ = (any ? bmin : -160.0) - margin;
      wbSpectrumYMax_ = (any ? bmax : 10.0) + margin;
    }
    if (wbSpectrumBlock_ && wbSpectrumRow_ < wbSpectrumBlockRows_ &&
        static_cast<int>(wbSpectrumBlock_->size()) ==
            wbSpectrumBlockRows_ * specN) {
      specMags = wbSpectrumBlock_->data() +
                 static_cast<size_t>(wbSpectrumRow_) * specN;
      animating = true;
      // 다음 프레임을 위해 한 행 전진(마지막 행에서 홀드; 새 사이클이 리셋).
      if (wbSpectrumRow_ + 1 < wbSpectrumBlockRows_) {
        ++wbSpectrumRow_;
      }
    }
  }

  // y축 한계는 전체 스펙트럼 기준으로 한 번만 계산해 모든 단이 공유하도록 한다.
  // 애니메이션 중이면 사이클 전체 기준 고정 스케일을 써서 행마다 튀지 않게
  // 한다.
  double yMin = -160.0;
  double yMax = 10.0;
  if (haveData) {
    if (yAxisAuto_) {
      if (animating) {
        yMin = wbSpectrumYMin_;
        yMax = wbSpectrumYMax_;
      } else {
        bool hasFinite = false;
        for (float value : snapshot.magnitudesDb) {
          if (std::isfinite(value)) {
            if (!hasFinite) {
              yMin = value;
              yMax = value;
              hasFinite = true;
            } else {
              yMin = std::min(yMin, static_cast<double>(value));
              yMax = std::max(yMax, static_cast<double>(value));
            }
          }
        }
        if (hasFinite) {
          const double margin = std::max(6.0, (yMax - yMin) * 0.15);
          yMin -= margin;
          yMax += margin;
        }
      }
    } else {
      yMin = static_cast<double>(yAxisMin_);
      yMax = static_cast<double>(yAxisMax_);
    }
    if (yAxisAuto_) {
      // 오토스케일 값을 10 단위로 스냅. 사이클마다 미세하게 흔들리면 눈금 라벨과
      // 축이 계속 다시 그려져 화면이 들썩인다.
      yMin = std::floor(yMin / 10.0) * 10.0;
      yMax = std::ceil(yMax / 10.0) * 10.0;
      if (yMax <= yMin) {
        yMax = yMin + 10.0;
      }
    }
  }

  const int tierCount =
      (spectrumTiers_ && haveData) ? std::max(1, spectrumTierCount_) : 1;
  const bool drawSgram = showSpectrogram_ && haveData;

  // --- 스펙트로그램 링버퍼 누적 (레이아웃과 무관하게 전체 폭 기준으로 갱신)
  // --- 링버퍼는 "전체 스펙트럼"을 한 행(row)으로 저장한다. 표시용 행렬은 열을
  // 화면 해상도 수준(kMaxSpectrogramCols)으로 max-pooling 다운샘플해 둔다.
  // 광대역에서 bins = 슬라이스수*2048로 폭발하는데, PlotHeatmap은 셀당 사각형을
  // 하나씩 그리므로 다운샘플 없이는 매 프레임 수백만 셀을 그려 극도로 느려진다.
  // 표시 행렬은 새 프레임이 들어왔을 때만(dirty) 재구성한다.
  float scaleMin = yAxisAuto_ ? -140.0f : yAxisMin_;
  float scaleMax = yAxisAuto_ ? 10.0f : yAxisMax_;
  if (drawSgram) {
    const int bins = specN;
    const int dsCols = std::min(bins, kMaxSpectrogramCols);
    const int dsRows = std::min(spectrogramRows_, kMaxSpectrogramRows);

    // Reset buffer on bin count change or rows change
    if (bins != spectrogramBins_ ||
        static_cast<int>(spectrogramBuf_.size()) != spectrogramRows_ * bins) {
      spectrogramBins_ = bins;
      spectrogramDsCols_ = dsCols;
      spectrogramDsRows_ = dsRows;
      spectrogramBuf_.assign(static_cast<size_t>(spectrogramRows_ * bins),
                             -180.0f);
      spectrogramDisp_.assign(
          static_cast<size_t>(dsRows) * static_cast<size_t>(dsCols), -180.0f);
      spectrogramHead_ = 0;
      spectrogramFill_ = 0;
      lastFftFrameCount_ = 0;
      spectrogramDirty_ = true;
    }

    // Detect streamer restart: fftFrameCount resets to 0
    if (snapshot.fftFrameCount < lastFftFrameCount_) {
      lastFftFrameCount_ = 0;
    }

    // 새 프레임마다 스펙트로그램에 행을 추가한다. WB 모드는 한 사이클의 dwell
    // 여러 행(spectrogramBlock)을 통째로 밀어넣어 시간구조를 보존하고, 그 외
    // 모드는 magnitudesDb 를 1행으로 넣는다.
    if (snapshot.fftFrameCount > lastFftFrameCount_) {
      lastFftFrameCount_ = snapshot.fftFrameCount;
      const bool haveBlock =
          snapshot.spectrogramBlock && snapshot.spectrogramBlockRows > 0 &&
          static_cast<int>(snapshot.spectrogramBlock->size()) ==
              snapshot.spectrogramBlockRows * bins;
      const int pushRows = haveBlock ? snapshot.spectrogramBlockRows : 1;
      for (int pr = 0; pr < pushRows; ++pr) {
        const float *src = haveBlock
                               ? snapshot.spectrogramBlock->data() + pr * bins
                               : snapshot.magnitudesDb.data();
        const int writeRow =
            (spectrogramHead_ + spectrogramFill_) % spectrogramRows_;
        std::copy_n(src, bins, spectrogramBuf_.begin() + writeRow * bins);
        if (spectrogramFill_ == spectrogramRows_) {
          spectrogramHead_ = (spectrogramHead_ + 1) % spectrogramRows_;
        } else {
          ++spectrogramFill_;
        }
      }
      spectrogramDirty_ = true;
    }

    // 표시 행렬 재구성: 새 프레임/리셋이 있었을 때만. 저장된 fill 행(가장
    // 오래된 것이 위쪽 index 0)을 표시 행 수(dsRows)로, 열을 dsCols 로 각각
    // max-pool 다운샘플한다(행·열 모두 피크 보존). fill 이 dsRows 보다 적으면
    // 그대로 두고 나머지는 패딩(-180).
    if (spectrogramDirty_ && spectrogramFill_ > 0) {
      std::fill(spectrogramDisp_.begin(), spectrogramDisp_.end(), -180.0f);
      const int srcRows = spectrogramFill_;
      for (int r = 0; r < dsRows; ++r) {
        // 이 표시 행이 덮는 원본(ring) 행 구간 [rLo, rHi).
        int rLo;
        int rHi;
        if (srcRows <= dsRows) {
          if (r >= srcRows) {
            continue; // 남는 표시 행은 패딩 유지
          }
          rLo = r;
          rHi = r + 1;
        } else {
          rLo = static_cast<int>(static_cast<int64_t>(r) * srcRows / dsRows);
          rHi =
              static_cast<int>(static_cast<int64_t>(r + 1) * srcRows / dsRows);
          if (rHi <= rLo) {
            rHi = rLo + 1;
          }
        }
        float *dst = spectrogramDisp_.data() +
                     static_cast<size_t>(r) * static_cast<size_t>(dsCols);
        for (int oc = 0; oc < dsCols; ++oc) {
          const int c0 =
              static_cast<int>(static_cast<int64_t>(bins) * oc / dsCols);
          const int c1 =
              std::max(c0 + 1, static_cast<int>(static_cast<int64_t>(bins) *
                                                (oc + 1) / dsCols));
          float m = -std::numeric_limits<float>::max();
          for (int sr = rLo; sr < rHi; ++sr) {
            const int ring = (spectrogramHead_ + sr) % spectrogramRows_;
            const float *srow =
                spectrogramBuf_.data() + static_cast<size_t>(ring) * bins;
            for (int c = c0; c < c1; ++c) {
              m = std::max(m, srow[c]);
            }
          }
          dst[oc] = m;
        }
      }
      spectrogramDirty_ = false;
    }

    // dB scale bounds (전체 스펙트럼 기준, 모든 단이 공유)
    if (yAxisAuto_) {
      for (float v : snapshot.magnitudesDb) {
        if (std::isfinite(v)) {
          scaleMin = std::min(scaleMin, v);
          scaleMax = std::max(scaleMax, v);
        }
      }
    }
  }

  // --- 높이 배분: 단(段)마다 [스펙트럼 + 바로 아래 그 구간 스펙트로그램] ---
  const bool sgramReady = drawSgram && spectrogramFill_ > 0;
  const float gap = ImGui::GetStyle().ItemSpacing.y;
  float specTierH;
  float sgramTierH = 0.0f;
  if (sgramReady) {
    // 단마다 2개 플롯(스펙트럼+스펙트로그램) -> 총 2*tierCount 개.
    const int plotRows = 2 * tierCount;
    const float totalPlotH = std::max(1.0f, availH - gap * (plotRows - 1));
    const float blockH = totalPlotH / static_cast<float>(tierCount);
    const float sgFrac =
        std::clamp(static_cast<float>(spectrogramHeightPct_), 20.0f, 80.0f) /
        100.0f;
    sgramTierH = std::max(1.0f, blockH * sgFrac);
    specTierH = std::max(1.0f, blockH * (1.0f - sgFrac));
  } else {
    const float totalPlotH =
        std::max(1.0f, availH - gap * static_cast<float>(tierCount - 1));
    specTierH = std::max(1.0f, totalPlotH / static_cast<float>(tierCount));
  }

  // --- 단별 렌더: 스펙트럼을 그리고, 바로 아래 같은 주파수 구간 스펙트로그램
  // ---
  const ImPlotColormap cmap =
      chartDark_ ? ImPlotColormap_Plasma : ImPlotColormap_Viridis;
  // 스택된 플롯들의 축 여백(왼쪽 Y라벨 폭 등)을 정렬해, 스펙트럼과
  // 스펙트로그램의 x축 시작/끝 위치가 세로로 정확히 맞도록 한다. Y라벨 폭이
  // 달라도(-160 vs 50) 플롯 영역이 어긋나지 않는다.
  const bool aligned = ImPlot::BeginAlignedPlots("##spectiers");
  for (int t = 0; t < tierCount; ++t) {
    int begin = 0;
    int count = haveData ? specN : 0;
    if (tierCount > 1) {
      // 주파수 구간을 tierCount 등분. 단 사이가 끊기지 않도록 경계 샘플을
      // 겹친다.
      begin = static_cast<int>(static_cast<int64_t>(specN) * t / tierCount);
      const int end =
          static_cast<int>(static_cast<int64_t>(specN) * (t + 1) / tierCount);
      const int last = std::min(end, specN - 1);
      count = std::max(0, last - begin + 1);
    }
    char specId[40];
    std::snprintf(specId, sizeof(specId), "##spectrum_tier%d", t);
    RenderSpectrumPlot(snapshot.frequencies.data(), specMags, specId, begin,
                       count, yMin, yMax, broadband, specTierH);
    if (sgramReady && count > 0) {
      // 이 단의 주파수 구간은 스펙트럼 단과 동일하게
      // freq[begin..begin+count-1]. 다운샘플된 표시 행렬에서 대응 열 구간을
      // 비례로 잘라 같은 x축에 그린다.
      const int dsCols = spectrogramDsCols_;
      const int dsBegin =
          static_cast<int>(static_cast<int64_t>(dsCols) * t / tierCount);
      const int dsEnd = (tierCount > 1)
                            ? static_cast<int>(static_cast<int64_t>(dsCols) *
                                               (t + 1) / tierCount)
                            : dsCols;
      const int dsCount = std::max(1, dsEnd - dsBegin);
      const double xMin = static_cast<double>(snapshot.frequencies[begin]);
      const double xMax =
          static_cast<double>(snapshot.frequencies[begin + count - 1]);
      char sgId[40];
      std::snprintf(sgId, sizeof(sgId), "##spectrogram_tier%d", t);
      ImPlot::PushColormap(cmap);
      RenderSpectrogramPlot(sgId, dsBegin, dsCount, xMin, xMax, sgramTierH,
                            scaleMin, scaleMax, broadband);
      ImPlot::PopColormap();
    }
  }
  if (aligned) {
    ImPlot::EndAlignedPlots();
  }
  ImPlot::PopStyleColor(5);

  ImGui::EndChild();
}

void App::RenderSpectrumPlot(const float *freqs, const float *mags,
                             const char *plotId, int i0, int count, double yMin,
                             double yMax, bool broadband, float height) {
  const ImVec2 plotSize(-1, height);
  if (!ImPlot::BeginPlot(plotId, plotSize)) {
    return;
  }
  ImPlot::SetupAxes(broadband ? "Frequency (MHz)" : "Frequency (Hz)",
                    broadband ? "Level" : "Magnitude (dB)");
  // y 눈금 라벨을 고정폭으로. 오토스케일로 자릿수가 바뀌면(-62 vs -198) 좌측
  // 여백이 달라지고, 그만큼 플롯 폭이 변해 x축 눈금 간격이 간헐적으로 튄다.
  ImPlot::SetupAxisFormat(ImAxis_Y1, "%6.0f");
  if (count > 0 && freqs != nullptr && mags != nullptr) {
    const double xMin = static_cast<double>(freqs[i0]);
    const double xMax = static_cast<double>(freqs[i0 + count - 1]);
    ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
    ImPlot::PushStyleColor(ImPlotCol_Line,
                           chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f)
                                      : ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
    ImPlot::PlotLine("Magnitude", freqs + i0, mags + i0, count);
    ImPlot::PopStyleColor();
  } else {
    ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -180.0, 10.0, ImGuiCond_Always);
  }
  ImPlot::EndPlot();
}

void App::RenderSpectrogramPlot(const char *plotId, int dsColBegin,
                                int dsColCount, double xMin, double xMax,
                                float height, float scaleMin, float scaleMax,
                                bool broadband) {
  const int dsCols = spectrogramDsCols_;
  const int dsRows = spectrogramDsRows_;
  if (dsCols <= 0 || dsRows <= 0 || dsColCount <= 0 || dsColBegin < 0 ||
      dsColBegin + dsColCount > dsCols ||
      static_cast<int>(spectrogramDisp_.size()) < dsRows * dsCols) {
    return;
  }

  // PlotHeatmap 은 셀당 사각형을 하나씩 그리므로, 실제로 보이는 것보다 더
  // 촘촘히 그리면 낭비다(특히 단 스펙트로그램은 높이가 ~수백 px 뿐). 그래서 이
  // 플롯의 실제 픽셀 크기(가로/세로)를 넘지 않도록 표시 행렬에서 다시 max-pool
  // 다운샘플해 타일을 만든다. 렌더 셀 수를 화면 픽셀 수 이하로 묶어 상수 시간에
  // 가깝게 한다.
  const float availW = ImGui::GetContentRegionAvail().x;
  // 행은 표시 상한(≤240)까지 전부 그린다(무조건 최대 240행). 열만 실제 플롯
  // 픽셀 폭을 넘지 않게 max-pool 다운샘플한다.
  const int renderRows = dsRows;
  const int renderCols =
      std::clamp(static_cast<int>(std::ceil(availW)), 1, dsColCount);

  const size_t tileSize =
      static_cast<size_t>(renderRows) * static_cast<size_t>(renderCols);
  if (spectrogramTile_.size() < tileSize) {
    spectrogramTile_.resize(tileSize);
  }
  // disp 의 [dsColBegin, dsColBegin+dsColCount) × [0, dsRows) 영역을
  // renderRows × renderCols 로 2D max-pool (행·열 모두 피크 보존).
  for (int rr = 0; rr < renderRows; ++rr) {
    const int r0 =
        static_cast<int>(static_cast<int64_t>(rr) * dsRows / renderRows);
    int r1 =
        static_cast<int>(static_cast<int64_t>(rr + 1) * dsRows / renderRows);
    if (r1 <= r0)
      r1 = r0 + 1;
    float *out = spectrogramTile_.data() +
                 static_cast<size_t>(rr) * static_cast<size_t>(renderCols);
    for (int cc = 0; cc < renderCols; ++cc) {
      const int c0 = dsColBegin + static_cast<int>(static_cast<int64_t>(cc) *
                                                   dsColCount / renderCols);
      int c1 = dsColBegin + static_cast<int>(static_cast<int64_t>(cc + 1) *
                                             dsColCount / renderCols);
      if (c1 <= c0)
        c1 = c0 + 1;
      float m = -std::numeric_limits<float>::max();
      for (int r = r0; r < r1; ++r) {
        const float *srow =
            spectrogramDisp_.data() +
            static_cast<size_t>(r) * static_cast<size_t>(dsCols);
        for (int c = c0; c < c1; ++c) {
          m = std::max(m, srow[c]);
        }
      }
      out[cc] = m;
    }
  }

  const ImVec2 spectroSize(-1, height);
  if (ImPlot::BeginPlot(plotId, spectroSize)) {
    ImPlot::SetupAxes(broadband ? "Frequency (MHz)" : "Frequency (Hz)",
                      "Time (frames)");
    // 스펙트럼 쪽과 같은 폭으로 맞춰 정렬 그룹의 좌측 여백을 고정한다.
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%6.0f");
    ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, static_cast<double>(renderRows),
                            ImGuiCond_Always);
    ImPlot::PlotHeatmap("##heatmap", spectrogramTile_.data(), renderRows,
                        renderCols, static_cast<double>(scaleMin),
                        static_cast<double>(scaleMax), nullptr,
                        ImPlotPoint(xMin, 0.0),
                        ImPlotPoint(xMax, static_cast<double>(renderRows)));
    ImPlot::EndPlot();
  }
}

void App::RenderZoneConfig() {
  ImGui::SetNextWindowSize(ImVec2(470, 540), ImGuiCond_FirstUseEver);
  bool open = showZoneConfig_;
  if (!ImGui::Begin("Zone Configuration (MSG-001)", &open)) {
    ImGui::End();
    showZoneConfig_ = open;
    return;
  }
  showZoneConfig_ = open;

  const ImVec4 accent = chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f)
                                   : ImVec4(0.169f, 0.424f, 0.690f, 1.0f);
  const ImVec4 warnColor = chartDark_ ? ImVec4(1.000f, 0.576f, 0.196f, 1.0f)
                                      : ImVec4(0.851f, 0.400f, 0.051f, 1.0f);
  const ImVec4 infoColor = chartDark_ ? ImVec4(0.353f, 0.953f, 0.647f, 1.0f)
                                      : ImVec4(0.102f, 0.549f, 0.200f, 1.0f);

  ImGui::TextColored(accent, "Sent to wb_scann on Start (REQ -> REP)");
  ImGui::PushItemWidth(-140.0f);
  ImGui::InputInt("Cmd port", &wbCmdPort_);
  wbCmdPort_ = std::clamp(wbCmdPort_, 1, 65535);
  ImGui::InputInt("Queue count (x4)", &wbQueueCount_);
  ImGui::InputInt("Frame count", &wbFrameCount_);
  ImGui::PopItemWidth();

  ImGui::Separator();
  ImGui::TextColored(accent, "Zones (%d / 64)",
                     static_cast<int>(wbZones_.size()));
  ImGui::TextColored(infoColor,
                     "stop is snapped to the nearest multiple of bw");

  int removeIdx = -1;
  ImGui::BeginChild("zonelist", ImVec2(0, -64), true);
  ImGui::PushItemWidth(-140.0f);
  for (int i = 0; i < static_cast<int>(wbZones_.size()); ++i) {
    ImGui::PushID(i);
    WbZone &z = wbZones_[static_cast<size_t>(i)];
    ImGui::TextColored(accent, "Zone %d", i);

    // start/stop 은 타이핑 중에 값을 건드리면 방해되므로, 편집이 끝난 시점에만
    // 스냅한다. bw 는 콤보 선택(단발 동작)이라 즉시 스냅.
    ImGui::InputInt("start (kHz)", &z.startFreqKHz);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      SnapZoneStop(z);
    }
    ImGui::InputInt("stop (kHz)", &z.stopFreqKHz);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      SnapZoneStop(z);
    }
    const int bwSel = std::clamp(z.bwCode, 0, 4);
    if (ImGui::BeginCombo("bw", constants::WB::BwLabel[bwSel])) {
      for (int b = 0; b < 5; ++b) {
        const bool selected = (z.bwCode == b);
        if (ImGui::Selectable(constants::WB::BwLabel[b], selected)) {
          z.bwCode = b;
          SnapZoneStop(z); // bw 가 바뀌면 stop 을 새 bw 배수로 맞춘다
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    // 스텝 수 / 실제 span 미리보기
    const int bwKHz = constants::WB::BW_KHz[bwSel];
    const long long span =
        static_cast<long long>(z.stopFreqKHz) - z.startFreqKHz;
    if (span > 0 && (span % bwKHz) == 0) {
      ImGui::TextColored(infoColor, "  %lld steps  (%.3f ~ %.3f MHz)",
                         span / bwKHz, z.startFreqKHz / 1000.0,
                         z.stopFreqKHz / 1000.0);
    } else {
      ImGui::TextColored(warnColor, "  not aligned to bw");
      ImGui::SameLine();
      if (ImGui::SmallButton("Snap")) {
        SnapZoneStop(z);
      }
    }

    if (wbZones_.size() > 1) {
      if (ImGui::SmallButton("Remove")) {
        removeIdx = i;
      }
    }
    ImGui::PopID();
    ImGui::Separator();
  }
  ImGui::PopItemWidth();
  ImGui::EndChild();

  if (removeIdx >= 0) {
    wbZones_.erase(wbZones_.begin() + removeIdx);
  }
  if (wbZones_.size() < 64) {
    if (ImGui::Button("Add zone", ImVec2(110, 0))) {
      WbZone z;
      if (!wbZones_.empty()) {
        // 직전 zone 바로 뒤를 이어받아 시작 (연속 대역 구성 편의)
        const WbZone &prev = wbZones_.back();
        z.bwCode = prev.bwCode;
        z.startFreqKHz = prev.stopFreqKHz;
        z.stopFreqKHz = prev.stopFreqKHz +
                        constants::WB::BW_KHz[std::clamp(z.bwCode, 0, 4)];
        SnapZoneStop(z);
      }
      wbZones_.push_back(z);
    }
    ImGui::SameLine();
  }
  if (ImGui::Button("Snap all", ImVec2(110, 0))) {
    for (WbZone &z : wbZones_) {
      SnapZoneStop(z);
    }
  }

  std::string zerr;
  if (ValidateZones(zerr)) {
    ImGui::TextColored(infoColor, "OK - %d zone(s), %zu bytes",
                       static_cast<int>(wbZones_.size()),
                       16 + 16 * wbZones_.size());
  } else {
    ImGui::TextColored(warnColor, "%s", zerr.c_str());
  }

  // --- Export / Import (뷰어 전용 *.zone 포맷) ---
  ImGui::Separator();
  ImGui::TextColored(accent, "Export / Import (*.zone)");
  if (ImGui::Button("Export as...", ImVec2(110, 0))) {
    if (BrowseZoneFile(true)) {
      std::string err;
      zoneFileStatus_ = SaveZones(zoneFilePath_.data(), err)
                            ? "Exported: " + std::string(zoneFilePath_.data())
                            : "Export failed: " + err;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Import...", ImVec2(110, 0))) {
    if (BrowseZoneFile(false)) {
      std::string err;
      if (LoadZones(zoneFilePath_.data(), err)) {
        std::string verr;
        zoneFileStatus_ =
            "Imported: " + std::string(zoneFilePath_.data()) +
            (ValidateZones(verr) ? "" : "  (warning: " + verr + ")");
      } else {
        zoneFileStatus_ = "Import failed: " + err;
      }
    }
  }
  // 경로를 이미 아는 경우 대화상자 없이 바로 덮어쓰기/다시읽기
  if (zoneFilePath_[0] != '\0') {
    if (ImGui::Button("Export", ImVec2(110, 0))) {
      std::string err;
      zoneFileStatus_ = SaveZones(zoneFilePath_.data(), err)
                            ? "Exported: " + std::string(zoneFilePath_.data())
                            : "Export failed: " + err;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(110, 0))) {
      std::string err;
      zoneFileStatus_ = LoadZones(zoneFilePath_.data(), err)
                            ? "Reloaded: " + std::string(zoneFilePath_.data())
                            : "Reload failed: " + err;
    }
    ImGui::TextWrapped("%s", zoneFilePath_.data());
  }
  if (!zoneFileStatus_.empty()) {
    ImGui::TextWrapped("%s", zoneFileStatus_.c_str());
  }

  ImGui::End();
}

void App::RenderConstellation(const StreamSnapshot &snapshot) {
  ImGui::SetNextWindowSize(ImVec2(420, 440), ImGuiCond_FirstUseEver);
  bool open = showConstellation_;
  if (!ImGui::Begin("Constellation", &open)) {
    ImGui::End();
    showConstellation_ = open;
    return;
  }
  showConstellation_ = open;

  if (chartDark_) {
    ImPlot::PushStyleColor(ImPlotCol_PlotBg,
                           ImVec4(0.031f, 0.035f, 0.055f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg,
                           ImVec4(0.059f, 0.078f, 0.125f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisText,
                           ImVec4(0.478f, 0.561f, 0.710f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                           ImVec4(0.102f, 0.145f, 0.251f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisTick,
                           ImVec4(0.165f, 0.227f, 0.345f, 1.0f));
  } else {
    ImPlot::PushStyleColor(ImPlotCol_PlotBg,
                           ImVec4(0.961f, 0.973f, 1.000f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg,
                           ImVec4(0.910f, 0.937f, 0.973f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisText,
                           ImVec4(0.227f, 0.314f, 0.439f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                           ImVec4(0.753f, 0.816f, 0.878f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisTick,
                           ImVec4(0.478f, 0.604f, 0.733f, 1.0f));
  }

  const bool hasData = !snapshot.iSamples.empty() &&
                       snapshot.iSamples.size() == snapshot.qSamples.size();

  if (ImPlot::BeginPlot("##constellation", ImVec2(-1, -1), ImPlotFlags_Equal)) {
    ImPlot::SetupAxes("I", "Q");
    ImPlot::SetupAxisLimits(ImAxis_X1, -1.5, 1.5, ImGuiCond_Once);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -1.5, 1.5, ImGuiCond_Once);

    if (hasData) {
      const int total = static_cast<int>(snapshot.iSamples.size());
      const int count = std::min(constellationPoints_, total);
      const int offset = total - count;

      ImPlot::PushStyleColor(ImPlotCol_MarkerFill,
                             chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 0.7f)
                                        : ImVec4(0.169f, 0.424f, 0.690f, 0.7f));
      ImPlot::PushStyleColor(ImPlotCol_MarkerOutline, ImVec4(0, 0, 0, 0));
      ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.0f);
      ImPlot::PlotScatter("IQ", snapshot.iSamples.data() + offset,
                          snapshot.qSamples.data() + offset, count);
      ImPlot::PopStyleColor(2);
    }
    ImPlot::EndPlot();
  }
  ImPlot::PopStyleColor(5);

  ImGui::End();
}