#include "App.h"

#include "Fft.h"

#include <Windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
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

void App::Render() {
    const StreamSnapshot snapshot = streamer_.Snapshot();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Bin TCP Spectrum", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    RenderControls(snapshot);
    ImGui::SameLine();
    RenderSpectrum(snapshot);

    ImGui::End();

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
    ImGui::TextUnformatted("Source");
    ImGui::SetNextItemWidth(-76);
    ImGui::InputText("##file", filePath_.data(), filePath_.size(), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        BrowseFile();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("TCP Server");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Bind IP", ip_.data(), ip_.size());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("Listen Port", &port_);

    ImGui::Separator();
    ImGui::TextUnformatted("Stream");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("Chunk bytes", &chunkBytes_);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("Interval ms", &sendIntervalMs_);
    ImGui::Checkbox("Loop file", &loop_);

    ImGui::Separator();
    ImGui::TextUnformatted("FFT");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("FFT size", &fftSize_);
    if (!Fft::IsPowerOfTwo(fftSize_)) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Power of two required");
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("Sample rate Hz", &sampleRateHz_);
    ImGui::SetNextItemWidth(-1);
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
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("Channels", &channels_);
    if (channels_ < 1) { channels_ = 1; }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputInt("Channel index", &channelIndex_);
    if (channelIndex_ < 0) { channelIndex_ = 0; }
    if (channelIndex_ >= channels_) { channelIndex_ = channels_ - 1; }
    if (channels_ > 1) {
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
            "Multi-ch: using ch %d of %d", channelIndex_, channels_ - 1);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Chart Style");
    if (ImGui::Button("Black", ImVec2(80, 24))) {
        chartDark_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("White", ImVec2(80, 24))) {
        chartDark_ = false;
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
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", snapshot.error.c_str());
    } else if (!lastError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", lastError_.c_str());
    }

    ImGui::EndChild();
}

void App::RenderSpectrum(const StreamSnapshot& snapshot) {
    ImGui::BeginChild("Spectrum", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Spectrum");
    ImGui::Separator();

    if (chartDark_) {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.06f, 0.06f, 0.06f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    } else {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    }

    const ImVec2 plotSize(-1, -1);
    if (ImPlot::BeginPlot("##spectrum", plotSize)) {
        ImPlot::SetupAxes("Frequency (Hz)", "Magnitude (dB)");
        if (!snapshot.frequencies.empty() && snapshot.frequencies.size() == snapshot.magnitudesDb.size()) {
            const double xMax = std::max(1.0, static_cast<double>(snapshot.frequencies.back()));
            double yMin = -160.0;
            double yMax = 10.0;
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
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, xMax, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
            ImPlot::PlotLine("Magnitude", snapshot.frequencies.data(), snapshot.magnitudesDb.data(), static_cast<int>(snapshot.magnitudesDb.size()));
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 1.0, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -180.0, 10.0, ImGuiCond_Always);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(4);

    ImGui::EndChild();
}