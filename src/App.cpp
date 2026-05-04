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
#include <fstream>
#include <sstream>
#include <string>

namespace {
int PushImGuiTheme(bool dark) {
    if (dark) {
        // Dark: Charcoal Blue + Amber accent
        ImGui::PushStyleColor(ImGuiCol_WindowBg,             ImVec4(0.059f, 0.067f, 0.090f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,              ImVec4(0.082f, 0.102f, 0.149f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,                 ImVec4(0.886f, 0.910f, 0.957f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled,         ImVec4(0.420f, 0.475f, 0.600f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border,               ImVec4(0.165f, 0.208f, 0.314f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,              ImVec4(0.110f, 0.141f, 0.220f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,       ImVec4(0.149f, 0.188f, 0.282f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,        ImVec4(0.188f, 0.235f, 0.353f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg,              ImVec4(0.059f, 0.067f, 0.090f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,        ImVec4(0.082f, 0.102f, 0.149f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,               ImVec4(0.145f, 0.200f, 0.337f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,        ImVec4(0.192f, 0.282f, 0.471f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,         ImVec4(0.239f, 0.349f, 0.580f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header,               ImVec4(0.145f, 0.200f, 0.337f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,        ImVec4(0.192f, 0.282f, 0.471f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,         ImVec4(0.239f, 0.349f, 0.580f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,            ImVec4(1.000f, 0.792f, 0.188f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,           ImVec4(1.000f, 0.792f, 0.188f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,     ImVec4(1.000f, 0.878f, 0.400f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator,            ImVec4(0.165f, 0.208f, 0.314f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SeparatorHovered,     ImVec4(1.000f, 0.792f, 0.188f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ImVec4(0.059f, 0.067f, 0.090f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ImVec4(0.165f, 0.208f, 0.314f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.239f, 0.349f, 0.580f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,              ImVec4(0.082f, 0.102f, 0.149f, 1.0f));
    } else {
        // Light: Soft White + Steel Blue accent
        ImGui::PushStyleColor(ImGuiCol_WindowBg,             ImVec4(0.933f, 0.945f, 0.961f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,              ImVec4(1.000f, 1.000f, 1.000f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,                 ImVec4(0.102f, 0.137f, 0.251f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled,         ImVec4(0.478f, 0.545f, 0.667f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border,               ImVec4(0.769f, 0.804f, 0.847f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,              ImVec4(0.878f, 0.906f, 0.937f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,       ImVec4(0.800f, 0.839f, 0.894f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,        ImVec4(0.718f, 0.769f, 0.847f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg,              ImVec4(0.933f, 0.945f, 0.961f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,        ImVec4(0.816f, 0.855f, 0.906f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,               ImVec4(0.820f, 0.890f, 0.969f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,        ImVec4(0.700f, 0.816f, 0.941f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,         ImVec4(0.573f, 0.733f, 0.910f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header,               ImVec4(0.749f, 0.816f, 0.906f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,        ImVec4(0.604f, 0.718f, 0.847f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,         ImVec4(0.467f, 0.627f, 0.800f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,            ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,           ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,     ImVec4(0.114f, 0.357f, 0.608f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator,            ImVec4(0.769f, 0.804f, 0.847f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SeparatorHovered,     ImVec4(0.169f, 0.424f, 0.690f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ImVec4(0.933f, 0.945f, 0.961f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ImVec4(0.769f, 0.804f, 0.847f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.467f, 0.627f, 0.800f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,              ImVec4(1.000f, 1.000f, 1.000f, 1.0f));
    }
    return 25;
}

const char* SampleFormatName(int index) {
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
}

void App::LoadSettings(const char* path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "filePath") {
            strncpy_s(filePath_.data(), filePath_.size(), val.c_str(), filePath_.size() - 1);
        } else if (key == "ip") {
            strncpy_s(ip_.data(), ip_.size(), val.c_str(), ip_.size() - 1);
        } else if (key == "port") {
            port_ = std::stoi(val);
        } else if (key == "fftSize") {
            const int v = std::stoi(val);
            // Snap to nearest valid power-of-two >= 256
            int snapped = 256;
            for (int s : { 256, 512, 1024, 2048, 4096, 8192, 16384 })
                if (s <= v) snapped = s;
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
        } else if (key == "chartDark") {
            chartDark_ = val == "1";
        } else if (key == "yAxisAuto") {
            yAxisAuto_ = val == "1";
        } else if (key == "yAxisMin") {
            yAxisMin_ = std::stof(val);
        } else if (key == "yAxisMax") {
            yAxisMax_ = std::stof(val);
        } else if (key == "showHistogram") {
            showSpectrogram_ = val == "1";
        } else if (key == "histogramBins") {
            spectrogramRows_ = std::stoi(val);
        } else if (key == "showSpectrogram") {
            showSpectrogram_ = val == "1";
        } else if (key == "spectrogramRows") {
            spectrogramRows_ = std::stoi(val);
        }
    }
}

void App::SaveSettings(const char* path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "[Settings]\n";
    f << "filePath=" << filePath_.data() << '\n';
    f << "ip=" << ip_.data() << '\n';
    f << "port=" << port_ << '\n';
    f << "fftSize=" << fftSize_ << '\n';
    f << "chunkBytes=" << chunkBytes_ << '\n';
    f << "sendIntervalMs=" << sendIntervalMs_ << '\n';
    f << "sampleRateHz=" << sampleRateHz_ << '\n';
    f << "loop=" << (loop_ ? 1 : 0) << '\n';
    f << "sampleFormatIndex=" << sampleFormatIndex_ << '\n';
    f << "channels=" << channels_ << '\n';
    f << "channelIndex=" << channelIndex_ << '\n';
    f << "chartDark=" << (chartDark_ ? 1 : 0) << '\n';
    f << "yAxisAuto=" << (yAxisAuto_ ? 1 : 0) << '\n';
    f << "yAxisMin=" << yAxisMin_ << '\n';
    f << "yAxisMax=" << yAxisMax_ << '\n';
    f << "showHistogram=" << (showSpectrogram_ ? 1 : 0) << '\n';
    f << "histogramBins=" << spectrogramRows_ << '\n';
    f << "showSpectrogram=" << (showSpectrogram_ ? 1 : 0) << '\n';
    f << "spectrogramRows=" << spectrogramRows_ << '\n';
}

void App::Render() {
    const StreamSnapshot snapshot = streamer_.Snapshot();

    const int themeColors = PushImGuiTheme(chartDark_);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Bin TCP Spectrum", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
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
    config.ip = ip_.data();
    config.port = static_cast<uint16_t>(std::clamp(port_, 1, 65535));
    config.fftSize = fftSize_;
    config.chunkBytes = chunkBytes_;
    config.sendIntervalMs = sendIntervalMs_;
    config.sampleRateHz = sampleRateHz_;
    config.loop = loop_;
    config.sampleFormat = static_cast<SampleFormat>(sampleFormatIndex_);
    config.channels = std::max(1, channels_);
    config.channelIndex = std::max(0, std::min(channelIndex_, config.channels - 1));
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

void App::RenderControls(const StreamSnapshot& snapshot) {
    ImGui::BeginChild("Controls", ImVec2(360, 0), true);
    const ImVec4 accent    = chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f) : ImVec4(0.169f, 0.424f, 0.690f, 1.0f);
    const ImVec4 warnColor = chartDark_ ? ImVec4(1.000f, 0.576f, 0.196f, 1.0f) : ImVec4(0.851f, 0.400f, 0.051f, 1.0f);
    const ImVec4 infoColor = chartDark_ ? ImVec4(0.353f, 0.953f, 0.647f, 1.0f) : ImVec4(0.102f, 0.549f, 0.200f, 1.0f);
    const ImVec4 errColor  = chartDark_ ? ImVec4(1.000f, 0.420f, 0.420f, 1.0f) : ImVec4(0.800f, 0.100f, 0.100f, 1.0f);

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
    }) + innerSpacing * 2.0f;
    ImGui::PushItemWidth(-maxLabelW);

    ImGui::TextColored(accent, "Source");
    ImGui::SetNextItemWidth(-76);
    ImGui::InputText("##file", filePath_.data(), filePath_.size(), ImGuiInputTextFlags_ReadOnly);
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

    ImGui::Separator();
    ImGui::TextColored(accent, "FFT");
    {
        static const int kFftSizes[] = { 256, 512, 1024, 2048, 4096, 8192, 16384 };
        static const char* kFftLabels[] = { "256", "512", "1024", "2048", "4096", "8192", "16384" };
        constexpr int kFftCount = 7;

        // Helper: snap fs/rbw to nearest valid FFT size >= 256
        auto rbwToFftSize = [&](float rbw) -> int {
            if (rbw <= 0.0f || sampleRateHz_ <= 0.0f) return fftSize_;
            const float ideal = sampleRateHz_ / rbw;
            int best = kFftSizes[0];
            float bestDist = std::abs(ideal - static_cast<float>(best));
            for (int i = 1; i < kFftCount; ++i) {
                const float d = std::abs(ideal - static_cast<float>(kFftSizes[i]));
                if (d < bestDist) { bestDist = d; best = kFftSizes[i]; }
            }
            return best;
        };

        int curIdx = 0;
        for (int i = 0; i < kFftCount; ++i)
            if (kFftSizes[i] == fftSize_) { curIdx = i; break; }

        if (ImGui::BeginCombo("FFT size", kFftLabels[curIdx])) {
            for (int i = 0; i < kFftCount; ++i) {
                const bool selected = (i == curIdx);
                if (ImGui::Selectable(kFftLabels[i], selected))
                    fftSize_ = kFftSizes[i];
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // RBW display/edit: RBW = fs / N
        float rbw = (sampleRateHz_ > 0.0f && fftSize_ > 0)
            ? sampleRateHz_ / static_cast<float>(fftSize_) : 0.0f;
        if (ImGui::InputFloat("RBW (Hz)", &rbw, 0.0f, 0.0f, "%.2f")) {
            fftSize_ = rbwToFftSize(rbw);
        }
    }
    ImGui::InputFloat("Sample rate Hz", &sampleRateHz_);
    if (ImGui::BeginCombo("Sample format", SampleFormatName(sampleFormatIndex_))) {
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
    if (channels_ < 1) { channels_ = 1; }
    ImGui::InputInt("Channel index", &channelIndex_);
    if (channelIndex_ < 0) { channelIndex_ = 0; }
    if (channelIndex_ >= channels_) { channelIndex_ = channels_ - 1; }
    if (channels_ > 1) {
        ImGui::TextColored(infoColor,
            "Multi-ch: using ch %d of %d", channelIndex_, channels_ - 1);
    }

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
    ImGui::TextColored(accent, "Y Axis");
    ImGui::Checkbox("Auto scale", &yAxisAuto_);
    if (!yAxisAuto_) {
        ImGui::InputFloat("Y min (dB)", &yAxisMin_);
        ImGui::InputFloat("Y max (dB)", &yAxisMax_);
    }

    ImGui::Separator();
    ImGui::TextColored(accent, "Spectrogram");
    ImGui::Checkbox("Show spectrogram", &showSpectrogram_);
    if (showSpectrogram_) {
        ImGui::InputInt("History rows", &spectrogramRows_);
        if (spectrogramRows_ < 10) spectrogramRows_ = 10;
        if (spectrogramRows_ > 1000) spectrogramRows_ = 1000;
    }

    ImGui::Separator();
    ImGui::TextColored(accent, "Constellation");
    ImGui::Checkbox("Show constellation", &showConstellation_);
    if (showConstellation_) {
        ImGui::InputInt("Points", &constellationPoints_);
        if (constellationPoints_ < 64) constellationPoints_ = 64;
        if (constellationPoints_ > 8192) constellationPoints_ = 8192;
        if (channels_ < 2) {
            ImGui::TextColored(warnColor, "Needs Channels >= 2 (IQ)");
        }
    }

    ImGui::Separator();
    if (!snapshot.running) {
        if (ImGui::Button("Start", ImVec2(110, 34))) {
            std::string error;
            if (!streamer_.Start(BuildConfig(), error)) {
                lastError_ = error;
            }
        }
    } else {
        if (ImGui::Button("Stop", ImVec2(110, 34))) {
            streamer_.Stop();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("ImPlot Demo", ImVec2(110, 34))) {
        showDemo_ = !showDemo_;
    }

    ImGui::Separator();
    ImGui::Text("Status: %s", snapshot.status.c_str());
    ImGui::Text("Listening: %s", snapshot.listening ? "yes" : "no");
    ImGui::Text("Connected: %s", snapshot.connected ? "yes" : "no");
    ImGui::Text("Packets: %llu", static_cast<unsigned long long>(snapshot.packetsSent));
    ImGui::Text("Bytes: %llu", static_cast<unsigned long long>(snapshot.bytesSent));

    if (!snapshot.error.empty()) {
        ImGui::TextColored(errColor, "%s", snapshot.error.c_str());
    } else if (!lastError_.empty()) {
        ImGui::TextColored(errColor, "%s", lastError_.c_str());
    }

    ImGui::PopItemWidth();
    ImGui::EndChild();
}

void App::RenderSpectrum(const StreamSnapshot& snapshot, float width) {
    ImGui::BeginChild("Spectrum", ImVec2(width, 0), true);
    {
        const ImVec4 specAccent = chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f) : ImVec4(0.169f, 0.424f, 0.690f, 1.0f);
        ImGui::TextColored(specAccent, "Spectrum");
    }
    ImGui::Separator();

    if (chartDark_) {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg,   ImVec4(0.031f, 0.035f, 0.055f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg,  ImVec4(0.059f, 0.078f, 0.125f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.478f, 0.561f, 0.710f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.102f, 0.145f, 0.251f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, ImVec4(0.165f, 0.227f, 0.345f, 1.0f));
    } else {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg,   ImVec4(0.961f, 0.973f, 1.000f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg,  ImVec4(0.910f, 0.937f, 0.973f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.227f, 0.314f, 0.439f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.753f, 0.816f, 0.878f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, ImVec4(0.478f, 0.604f, 0.733f, 1.0f));
    }

    const float availH = ImGui::GetContentRegionAvail().y;
    const float histH = showSpectrogram_ ? std::max(120.0f, availH * 0.28f) : 0.0f;
    const float specH = availH - histH - (showSpectrogram_ ? ImGui::GetStyle().ItemSpacing.y : 0.0f);

    const ImVec2 plotSize(-1, specH);
    if (ImPlot::BeginPlot("##spectrum", plotSize)) {
        ImPlot::SetupAxes("Frequency (Hz)", "Magnitude (dB)");
        if (!snapshot.frequencies.empty() && snapshot.frequencies.size() == snapshot.magnitudesDb.size()) {
            const double xMin = static_cast<double>(snapshot.frequencies.front());
            const double xMax = static_cast<double>(snapshot.frequencies.back());
            double yMin = -160.0;
            double yMax = 10.0;
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
            ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
            ImPlot::PushStyleColor(ImPlotCol_Line, chartDark_
                ? ImVec4(1.000f, 0.792f, 0.188f, 1.0f)
                : ImVec4(0.169f, 0.424f, 0.690f, 1.0f));
            ImPlot::PlotLine("Magnitude", snapshot.frequencies.data(), snapshot.magnitudesDb.data(), static_cast<int>(snapshot.magnitudesDb.size()));
            ImPlot::PopStyleColor();
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -180.0, 10.0, ImGuiCond_Always);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(5);

    // Spectrogram: accumulate ring buffer
    if (showSpectrogram_ && !snapshot.magnitudesDb.empty()) {
        const int bins = static_cast<int>(snapshot.magnitudesDb.size());

        // Reset buffer on bin count change or rows change
        if (bins != spectrogramBins_ ||
            static_cast<int>(spectrogramBuf_.size()) != spectrogramRows_ * bins) {
            spectrogramBins_ = bins;
            spectrogramBuf_.assign(static_cast<size_t>(spectrogramRows_ * bins), -180.0f);
            spectrogramDisp_.clear();
            spectrogramHead_ = 0;
            spectrogramFill_ = 0;
            lastFftFrameCount_ = 0;
        }

        // Detect streamer restart: fftFrameCount resets to 0
        if (snapshot.fftFrameCount < lastFftFrameCount_) {
            lastFftFrameCount_ = 0;
        }

        // Push one row per new FFT frame
        if (snapshot.fftFrameCount > lastFftFrameCount_) {
            lastFftFrameCount_ = snapshot.fftFrameCount;
            const int writeRow = (spectrogramHead_ + spectrogramFill_) % spectrogramRows_;
            std::copy(snapshot.magnitudesDb.begin(), snapshot.magnitudesDb.end(),
                spectrogramBuf_.begin() + writeRow * bins);
            if (spectrogramFill_ == spectrogramRows_) {
                spectrogramHead_ = (spectrogramHead_ + 1) % spectrogramRows_;
            } else {
                ++spectrogramFill_;
            }
        }

        if (spectrogramFill_ > 0) {
            // Build linear display buffer: oldest row at top (index 0)
            // Reuse pre-allocated buffer to avoid per-frame heap allocation
            const size_t dispSize = static_cast<size_t>(spectrogramRows_ * bins);
            if (spectrogramDisp_.size() != dispSize) {
                spectrogramDisp_.assign(dispSize, -180.0f);
            }
            std::fill(spectrogramDisp_.begin(), spectrogramDisp_.end(), -180.0f);
            for (int r = 0; r < spectrogramFill_; ++r) {
                const int srcRow = (spectrogramHead_ + r) % spectrogramRows_;
                std::copy_n(spectrogramBuf_.begin() + srcRow * bins,
                    bins, spectrogramDisp_.begin() + r * bins);
            }

            // dB scale bounds
            float scaleMin = yAxisAuto_ ? -140.0f : yAxisMin_;
            float scaleMax = yAxisAuto_ ?   10.0f : yAxisMax_;
            if (yAxisAuto_) {
                for (float v : snapshot.magnitudesDb) {
                    if (std::isfinite(v)) {
                        scaleMin = std::min(scaleMin, v);
                        scaleMax = std::max(scaleMax, v);
                    }
                }
            }

            if (chartDark_) {
                ImPlot::PushStyleColor(ImPlotCol_PlotBg,   ImVec4(0.031f, 0.035f, 0.055f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_FrameBg,  ImVec4(0.059f, 0.078f, 0.125f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.478f, 0.561f, 0.710f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.102f, 0.145f, 0.251f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_AxisTick, ImVec4(0.165f, 0.227f, 0.345f, 1.0f));
            } else {
                ImPlot::PushStyleColor(ImPlotCol_PlotBg,   ImVec4(0.961f, 0.973f, 1.000f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_FrameBg,  ImVec4(0.910f, 0.937f, 0.973f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.227f, 0.314f, 0.439f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.753f, 0.816f, 0.878f, 1.0f));
                ImPlot::PushStyleColor(ImPlotCol_AxisTick, ImVec4(0.478f, 0.604f, 0.733f, 1.0f));
            }

            const ImPlotColormap cmap = chartDark_ ? ImPlotColormap_Plasma : ImPlotColormap_Viridis;
            ImPlot::PushColormap(cmap);

            const double xMin2 = snapshot.frequencies.empty()
                ? 0.0 : static_cast<double>(snapshot.frequencies.front());
            const double xMax2 = snapshot.frequencies.empty()
                ? 1.0 : static_cast<double>(snapshot.frequencies.back());

            const ImVec2 spectroSize(-1, histH);
            if (ImPlot::BeginPlot("##spectrogram", spectroSize)) {
                ImPlot::SetupAxes("Frequency (Hz)", "Time (frames)");
                ImPlot::SetupAxisLimits(ImAxis_X1, xMin2, xMax2, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0,
                    static_cast<double>(spectrogramRows_), ImGuiCond_Always);
                // rows=spectrogramRows_, cols=bins
                // bounds: (xMin2,yMin)=(freq_start,0) → (xMax2, spectrogramRows_)
                ImPlot::PlotHeatmap("##heatmap",
                    spectrogramDisp_.data(), spectrogramRows_, bins,
                    static_cast<double>(scaleMin), static_cast<double>(scaleMax),
                    nullptr,
                    ImPlotPoint(xMin2, 0.0),
                    ImPlotPoint(xMax2, static_cast<double>(spectrogramRows_)));
                ImPlot::EndPlot();
            }
            ImPlot::PopColormap();
            ImPlot::PopStyleColor(5);
        }
    }

    ImGui::EndChild();
}

void App::RenderConstellation(const StreamSnapshot& snapshot) {
    ImGui::SetNextWindowSize(ImVec2(420, 440), ImGuiCond_FirstUseEver);
    bool open = showConstellation_;
    if (!ImGui::Begin("Constellation", &open)) {
        ImGui::End();
        showConstellation_ = open;
        return;
    }
    showConstellation_ = open;

    if (chartDark_) {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg,   ImVec4(0.031f, 0.035f, 0.055f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg,  ImVec4(0.059f, 0.078f, 0.125f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.478f, 0.561f, 0.710f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.102f, 0.145f, 0.251f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, ImVec4(0.165f, 0.227f, 0.345f, 1.0f));
    } else {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg,   ImVec4(0.961f, 0.973f, 1.000f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg,  ImVec4(0.910f, 0.937f, 0.973f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.227f, 0.314f, 0.439f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.753f, 0.816f, 0.878f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisTick, ImVec4(0.478f, 0.604f, 0.733f, 1.0f));
    }

    const bool hasData = !snapshot.iSamples.empty()
        && snapshot.iSamples.size() == snapshot.qSamples.size();

    if (ImPlot::BeginPlot("##constellation", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("I", "Q");
        ImPlot::SetupAxisLimits(ImAxis_X1, -1.5, 1.5, ImGuiCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1.5, 1.5, ImGuiCond_Once);

        if (hasData) {
            const int total  = static_cast<int>(snapshot.iSamples.size());
            const int count  = std::min(constellationPoints_, total);
            const int offset = total - count;

            ImPlot::PushStyleColor(ImPlotCol_MarkerFill,
                chartDark_ ? ImVec4(1.000f, 0.792f, 0.188f, 0.7f)
                           : ImVec4(0.169f, 0.424f, 0.690f, 0.7f));
            ImPlot::PushStyleColor(ImPlotCol_MarkerOutline, ImVec4(0, 0, 0, 0));
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.0f);
            ImPlot::PlotScatter("IQ",
                snapshot.iSamples.data() + offset,
                snapshot.qSamples.data() + offset,
                count);
            ImPlot::PopStyleColor(2);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(5);

    ImGui::End();
}