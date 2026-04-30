#pragma once

#include "BinStreamer.h"

#include <array>
#include <string>

class App {
public:
    void Render();

private:
    StreamConfig BuildConfig() const;
    void BrowseFile();
    void RenderControls(const StreamSnapshot& snapshot);
    void RenderSpectrum(const StreamSnapshot& snapshot);

    std::array<char, 260> filePath_{};
    std::array<char, 64> ip_{ '0', '.', '0', '.', '0', '.', '0', '\0' };
    int port_ = 9000;
    int fftSize_ = 1024;
    int chunkBytes_ = 4096;
    int sendIntervalMs_ = 10;
    float sampleRateHz_ = 48000.0f;
    bool loop_ = true;
    int sampleFormatIndex_ = static_cast<int>(SampleFormat::Int16LE);
    int channels_ = 1;
    int channelIndex_ = 0;
    bool showDemo_ = false;
    bool chartDark_ = true;
    std::string lastError_;
    BinStreamer streamer_;
};