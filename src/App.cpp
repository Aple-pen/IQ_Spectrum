#include "App.h"

#include "Fft.h"

#include <Windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <implot.h>

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
  f << "freqOffsetHz=" << freqOffsetHz_ << '\n';
  f << "frequencyIndex=" << frequencyIndex_ << '\n';
}

IqStream &App::Active() {
  return mode_ == static_cast<int>(Mode::Receive)
             ? static_cast<IqStream &>(receiver_)
             : static_cast<IqStream &>(streamer_);
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
  }

  const StreamSnapshot snapshot = Active().Snapshot();

  const int themeColors = PushImGuiTheme(chartDark_);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
  ImGui::Begin("Bin TCP Spectrum", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  RenderControls(snapshot);
  ImGui::SameLine();
  RenderSpectrum(snapshot, 0.0f);

  ImGui::End();
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
  if (mode_ == static_cast<int>(Mode::Receive)) {
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

  // 모드 선택: 재생(서버) vs 실시간 수신(클라이언트). 실행 중에는 잠금.
  ImGui::TextColored(accent, "Mode");
  ImGui::BeginDisabled(snapshot.streamRunning);
  ImGui::RadioButton("Playback (Server)", &mode_,
                     static_cast<int>(Mode::Playback));
  ImGui::SameLine();
  ImGui::RadioButton("Receive (Client)", &mode_,
                     static_cast<int>(Mode::Receive));
  ImGui::EndDisabled();

  const bool receiveMode = (mode_ == static_cast<int>(Mode::Receive));

  ImGui::Separator();
  if (!receiveMode) {
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
    if (!snapshot.hmftValid) {
      ImGui::TextColored(warnColor, "Best with HMFT wideband stream");
    }
  }

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
    ImGui::TextColored(infoColor, "Multi-ch: using ch %d of %d", channelIndex_,
                       channels_ - 1);
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
      ImGui::TextColored(infoColor, "seq %u  BW %s  Center %.3f MHz",
                         snapshot.hmftSeq, bw,
                         static_cast<double>(snapshot.hmftCenterKHz) / 1000.0);
    } else {
      ImGui::TextColored(warnColor, "No HMFT magic parsed yet");
    }
  }
  } // channels_ == 1

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
    ImGui::InputInt("History rows", &spectrogramRows_);
    if (spectrogramRows_ < 10)
      spectrogramRows_ = 10;
    if (spectrogramRows_ > 1000)
      spectrogramRows_ = 1000;
  }

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

  // 여기까지가 캡처 중 잠금 대상. 아래 Start/Stop·Capture 버튼은 활성 유지.
  ImGui::EndDisabled();

  ImGui::Separator();
  if (!snapshot.streamRunning) {
    if (ImGui::Button("Start", ImVec2(110, 34))) {
      std::string error;
      const bool ok = receiveMode ? receiver_.Start(BuildConfig(), error)
                                  : streamer_.Start(BuildConfig(), error);
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
  if (!receiveMode) {
    ImGui::Text("Listening: %s", snapshot.listening ? "yes" : "no");
  }
  ImGui::Text("Connected: %s", snapshot.connected ? "yes" : "no");
  ImGui::Text("%s: %llu", receiveMode ? "Recv packets" : "Sent packets",
              static_cast<unsigned long long>(snapshot.packetsSent));
  ImGui::Text("%s: %llu", receiveMode ? "Recv bytes" : "Sent bytes",
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

  // y축 한계는 전체 스펙트럼 기준으로 한 번만 계산해 모든 단이 공유하도록 한다.
  double yMin = -160.0;
  double yMax = 10.0;
  if (haveData) {
    if (yAxisAuto_) {
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
    } else {
      yMin = static_cast<double>(yAxisMin_);
      yMax = static_cast<double>(yAxisMax_);
    }
  }

  const int tierCount =
      (spectrumTiers_ && haveData) ? std::max(1, spectrumTierCount_) : 1;
  const bool drawSgram = showSpectrogram_ && haveData;

  // --- 스펙트로그램 링버퍼 누적 (레이아웃과 무관하게 전체 폭 기준으로 갱신) ---
  // 링버퍼는 "전체 스펙트럼"을 한 행(row)으로 저장한다. 표시용 행렬은 열을
  // 화면 해상도 수준(kMaxSpectrogramCols)으로 max-pooling 다운샘플해 둔다.
  // 광대역에서 bins = 슬라이스수*2048로 폭발하는데, PlotHeatmap은 셀당 사각형을
  // 하나씩 그리므로 다운샘플 없이는 매 프레임 수백만 셀을 그려 극도로 느려진다.
  // 표시 행렬은 새 프레임이 들어왔을 때만(dirty) 재구성한다.
  float scaleMin = yAxisAuto_ ? -140.0f : yAxisMin_;
  float scaleMax = yAxisAuto_ ? 10.0f : yAxisMax_;
  if (drawSgram) {
    const int bins = specN;
    const int dsCols = std::min(bins, kMaxSpectrogramCols);

    // Reset buffer on bin count change or rows change
    if (bins != spectrogramBins_ ||
        static_cast<int>(spectrogramBuf_.size()) != spectrogramRows_ * bins) {
      spectrogramBins_ = bins;
      spectrogramDsCols_ = dsCols;
      spectrogramBuf_.assign(static_cast<size_t>(spectrogramRows_ * bins),
                             -180.0f);
      spectrogramDisp_.assign(
          static_cast<size_t>(spectrogramRows_) * static_cast<size_t>(dsCols),
          -180.0f);
      spectrogramHead_ = 0;
      spectrogramFill_ = 0;
      lastFftFrameCount_ = 0;
      spectrogramDirty_ = true;
    }

    // Detect streamer restart: fftFrameCount resets to 0
    if (snapshot.fftFrameCount < lastFftFrameCount_) {
      lastFftFrameCount_ = 0;
    }

    // Push one row per new FFT frame
    if (snapshot.fftFrameCount > lastFftFrameCount_) {
      lastFftFrameCount_ = snapshot.fftFrameCount;
      const int writeRow =
          (spectrogramHead_ + spectrogramFill_) % spectrogramRows_;
      std::copy(snapshot.magnitudesDb.begin(), snapshot.magnitudesDb.end(),
                spectrogramBuf_.begin() + writeRow * bins);
      if (spectrogramFill_ == spectrogramRows_) {
        spectrogramHead_ = (spectrogramHead_ + 1) % spectrogramRows_;
      } else {
        ++spectrogramFill_;
      }
      spectrogramDirty_ = true;
    }

    // 표시 행렬 재구성: 새 프레임/리셋이 있었을 때만. 가장 오래된 행이 위쪽
    // (index 0). 열은 원본 [c0, c1) 그룹의 최댓값으로 다운샘플(피크 보존).
    if (spectrogramDirty_ && spectrogramFill_ > 0) {
      std::fill(spectrogramDisp_.begin(), spectrogramDisp_.end(), -180.0f);
      for (int r = 0; r < spectrogramFill_; ++r) {
        const int srcRow = (spectrogramHead_ + r) % spectrogramRows_;
        const float *src =
            spectrogramBuf_.data() + static_cast<size_t>(srcRow) * bins;
        float *dst = spectrogramDisp_.data() +
                     static_cast<size_t>(r) * static_cast<size_t>(dsCols);
        if (dsCols == bins) {
          std::copy_n(src, bins, dst);
        } else {
          for (int oc = 0; oc < dsCols; ++oc) {
            const int c0 =
                static_cast<int>(static_cast<int64_t>(bins) * oc / dsCols);
            const int c1 = std::max(
                c0 + 1,
                static_cast<int>(static_cast<int64_t>(bins) * (oc + 1) / dsCols));
            float m = src[c0];
            for (int c = c0 + 1; c < c1; ++c) {
              m = std::max(m, src[c]);
            }
            dst[oc] = m;
          }
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
    sgramTierH = std::max(1.0f, blockH * 0.4f);
    specTierH = std::max(1.0f, blockH * 0.6f);
  } else {
    const float totalPlotH =
        std::max(1.0f, availH - gap * static_cast<float>(tierCount - 1));
    specTierH = std::max(1.0f, totalPlotH / static_cast<float>(tierCount));
  }

  // --- 단별 렌더: 스펙트럼을 그리고, 바로 아래 같은 주파수 구간 스펙트로그램 ---
  const ImPlotColormap cmap =
      chartDark_ ? ImPlotColormap_Plasma : ImPlotColormap_Viridis;
  // 스택된 플롯들의 축 여백(왼쪽 Y라벨 폭 등)을 정렬해, 스펙트럼과 스펙트로그램의
  // x축 시작/끝 위치가 세로로 정확히 맞도록 한다. Y라벨 폭이 달라도(-160 vs 50)
  // 플롯 영역이 어긋나지 않는다.
  const bool aligned = ImPlot::BeginAlignedPlots("##spectiers");
  for (int t = 0; t < tierCount; ++t) {
    int begin = 0;
    int count = haveData ? specN : 0;
    if (tierCount > 1) {
      // 주파수 구간을 tierCount 등분. 단 사이가 끊기지 않도록 경계 샘플을 겹친다.
      begin = static_cast<int>(static_cast<int64_t>(specN) * t / tierCount);
      const int end =
          static_cast<int>(static_cast<int64_t>(specN) * (t + 1) / tierCount);
      const int last = std::min(end, specN - 1);
      count = std::max(0, last - begin + 1);
    }
    char specId[40];
    std::snprintf(specId, sizeof(specId), "##spectrum_tier%d", t);
    RenderSpectrumPlot(snapshot, specId, begin, count, yMin, yMax, broadband,
                       specTierH);
    if (sgramReady && count > 0) {
      // 이 단의 주파수 구간은 스펙트럼 단과 동일하게 freq[begin..begin+count-1].
      // 다운샘플된 표시 행렬에서 대응 열 구간을 비례로 잘라 같은 x축에 그린다.
      const int dsCols = spectrogramDsCols_;
      const int dsBegin =
          static_cast<int>(static_cast<int64_t>(dsCols) * t / tierCount);
      const int dsEnd =
          (tierCount > 1)
              ? static_cast<int>(static_cast<int64_t>(dsCols) * (t + 1) /
                                 tierCount)
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

void App::RenderSpectrumPlot(const StreamSnapshot &snapshot, const char *plotId,
                            int i0, int count, double yMin, double yMax,
                            bool broadband, float height) {
  const ImVec2 plotSize(-1, height);
  if (!ImPlot::BeginPlot(plotId, plotSize)) {
    return;
  }
  ImPlot::SetupAxes(broadband ? "Frequency (MHz)" : "Frequency (Hz)",
                    broadband ? "Level" : "Magnitude (dB)");
  if (count > 0) {
    const double xMin = static_cast<double>(snapshot.frequencies[i0]);
    const double xMax =
        static_cast<double>(snapshot.frequencies[i0 + count - 1]);
    ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
    ImPlot::PushStyleColor(ImPlotCol_Line,
                           chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f)
                                      : ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
    ImPlot::PlotLine("Magnitude", snapshot.frequencies.data() + i0,
                     snapshot.magnitudesDb.data() + i0, count);
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
  if (dsCols <= 0 || dsColCount <= 0 || dsColBegin < 0 ||
      dsColBegin + dsColCount > dsCols ||
      static_cast<int>(spectrogramDisp_.size()) < spectrogramRows_ * dsCols) {
    return;
  }

  // 다운샘플된 표시 행렬(row-major [row*dsCols + col])에서
  // [dsColBegin, dsColBegin+dsColCount) 열만 잘라 연속 버퍼로 모은다.
  // PlotHeatmap이 stride 없는 연속 배열을 요구하기 때문. 단마다 열 수가 ±1
  // 달라질 수 있어 버퍼는 최대 크기로만 키운다.
  const size_t tileSize =
      static_cast<size_t>(spectrogramRows_) * static_cast<size_t>(dsColCount);
  if (spectrogramTile_.size() < tileSize) {
    spectrogramTile_.resize(tileSize);
  }
  for (int r = 0; r < spectrogramRows_; ++r) {
    const float *src = spectrogramDisp_.data() +
                       static_cast<size_t>(r) * static_cast<size_t>(dsCols) +
                       dsColBegin;
    std::copy_n(src, dsColCount,
                spectrogramTile_.begin() +
                    static_cast<std::ptrdiff_t>(
                        static_cast<size_t>(r) *
                        static_cast<size_t>(dsColCount)));
  }

  const ImVec2 spectroSize(-1, height);
  if (ImPlot::BeginPlot(plotId, spectroSize)) {
    ImPlot::SetupAxes(broadband ? "Frequency (MHz)" : "Frequency (Hz)",
                      "Time (frames)");
    ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0,
                            static_cast<double>(spectrogramRows_),
                            ImGuiCond_Always);
    ImPlot::PlotHeatmap("##heatmap", spectrogramTile_.data(), spectrogramRows_,
                        dsColCount, static_cast<double>(scaleMin),
                        static_cast<double>(scaleMax), nullptr,
                        ImPlotPoint(xMin, 0.0),
                        ImPlotPoint(xMax, static_cast<double>(spectrogramRows_)));
    ImPlot::EndPlot();
  }
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