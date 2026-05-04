#pragma once

#include "BinStreamer.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class App {
public:
    void Render();
    void LoadSettings(const char* path = "settings.ini");
    void SaveSettings(const char* path = "settings.ini") const;

private:
    StreamConfig BuildConfig() const;
    void BrowseFile();
    void RenderControls(const StreamSnapshot& snapshot);
    void RenderSpectrum(const StreamSnapshot& snapshot, float width);
    void RenderConstellation(const StreamSnapshot& snapshot);

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
    bool yAxisAuto_ = true;
    float yAxisMin_ = -160.0f;
    float yAxisMax_ = 10.0f;
    bool showSpectrogram_ = false;
    int spectrogramRows_ = 200;          // number of time rows to keep
    bool showConstellation_ = false;
    int constellationPoints_ = 1024;     // number of IQ points to display
    std::vector<float> spectrogramBuf_;  // ring buffer [row * bins + bin]
    std::vector<float> spectrogramDisp_; // linear display buffer (reused each frame)
    int spectrogramHead_ = 0;            // index of oldest row
    int spectrogramFill_ = 0;            // number of valid rows written
    int spectrogramBins_ = 0;            // current bin count (detect reset)
    uint64_t lastFftFrameCount_ = 0;
    std::string lastError_;
    BinStreamer streamer_;
};