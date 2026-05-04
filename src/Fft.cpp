#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float Pi = 3.14159265358979323846f;

float Hann(int index, int size) {
    if (size <= 1) {
        return 1.0f;
    }
    return 0.5f * (1.0f - std::cos((2.0f * Pi * static_cast<float>(index)) / static_cast<float>(size - 1)));
}
}

bool Fft::IsPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::vector<float> Fft::MagnitudeSpectrum(const std::vector<float>& samples, int fftSize, float sampleRateHz) {
    if (!IsPowerOfTwo(fftSize) || static_cast<int>(samples.size()) < fftSize) {
        return {};
    }

    std::vector<std::complex<float>> values(static_cast<size_t>(fftSize));
    const int offset = static_cast<int>(samples.size()) - fftSize;
    float mean = 0.0f;
    for (int index = 0; index < fftSize; ++index) {
        const float sample = samples[static_cast<size_t>(offset + index)];
        mean += std::isfinite(sample) ? sample : 0.0f;
    }
    mean /= static_cast<float>(fftSize);

    for (int index = 0; index < fftSize; ++index) {
        float sample = samples[static_cast<size_t>(offset + index)] - mean;
        if (!std::isfinite(sample)) {
            sample = 0.0f;
        }
        values[static_cast<size_t>(index)] = std::complex<float>(sample * Hann(index, fftSize), 0.0f);
    }

    Transform(values);

    const int bins = fftSize / 2;
    std::vector<float> magnitudes(static_cast<size_t>(bins));
    const float scale = 2.0f / static_cast<float>(fftSize);
    for (int bin = 0; bin < bins; ++bin) {
        const float magnitude = std::abs(values[static_cast<size_t>(bin)]) * scale;
        const float db = 20.0f * std::log10(std::max(magnitude, 1.0e-9f));
        magnitudes[static_cast<size_t>(bin)] = std::isfinite(db) ? std::max(db, -180.0f) : -180.0f;
    }

    (void)sampleRateHz;
    return magnitudes;
}

std::vector<float> Fft::MagnitudeSpectrumIQ(const std::vector<float>& iSamples, const std::vector<float>& qSamples, int fftSize, float sampleRateHz) {
    if (!IsPowerOfTwo(fftSize)
        || static_cast<int>(iSamples.size()) < fftSize
        || static_cast<int>(qSamples.size()) < fftSize) {
        return {};
    }

    // DC removal (mean subtraction per component)
    const int offsetI = static_cast<int>(iSamples.size()) - fftSize;
    const int offsetQ = static_cast<int>(qSamples.size()) - fftSize;
    float meanI = 0.0f, meanQ = 0.0f;
    for (int n = 0; n < fftSize; ++n) {
        const float si = iSamples[static_cast<size_t>(offsetI + n)];
        const float sq = qSamples[static_cast<size_t>(offsetQ + n)];
        meanI += std::isfinite(si) ? si : 0.0f;
        meanQ += std::isfinite(sq) ? sq : 0.0f;
    }
    meanI /= static_cast<float>(fftSize);
    meanQ /= static_cast<float>(fftSize);

    std::vector<std::complex<float>> values(static_cast<size_t>(fftSize));
    for (int n = 0; n < fftSize; ++n) {
        float si = iSamples[static_cast<size_t>(offsetI + n)] - meanI;
        float sq = qSamples[static_cast<size_t>(offsetQ + n)] - meanQ;
        if (!std::isfinite(si)) si = 0.0f;
        if (!std::isfinite(sq)) sq = 0.0f;
        const float w = Hann(n, fftSize);
        values[static_cast<size_t>(n)] = std::complex<float>(si * w, sq * w);
    }

    Transform(values);

    // Full N bins, scale = 1/N for complex
    const float scale = 1.0f / static_cast<float>(fftSize);
    std::vector<float> magnitudes(static_cast<size_t>(fftSize));
    for (int k = 0; k < fftSize; ++k) {
        const float magnitude = std::abs(values[static_cast<size_t>(k)]) * scale;
        const float db = 20.0f * std::log10(std::max(magnitude, 1.0e-9f));
        magnitudes[static_cast<size_t>(k)] = std::isfinite(db) ? std::max(db, -180.0f) : -180.0f;
    }

    // fftshift: rotate so DC moves to center (-fs/2 .. 0 .. +fs/2)
    std::rotate(magnitudes.begin(), magnitudes.begin() + fftSize / 2, magnitudes.end());

    (void)sampleRateHz;
    return magnitudes;
}

void Fft::Transform(std::vector<std::complex<float>>& values) {
    const size_t size = values.size();
    size_t bitReversed = 0;
    for (size_t index = 1; index < size; ++index) {
        size_t bit = size >> 1;
        while ((bitReversed & bit) != 0U) {
            bitReversed ^= bit;
            bit >>= 1;
        }
        bitReversed ^= bit;
        if (index < bitReversed) {
            std::swap(values[index], values[bitReversed]);
        }
    }

    for (size_t length = 2; length <= size; length <<= 1) {
        const float angle = -2.0f * Pi / static_cast<float>(length);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t index = 0; index < size; index += length) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < length / 2; ++j) {
                const std::complex<float> even = values[index + j];
                const std::complex<float> odd = values[index + j + length / 2] * w;
                values[index + j] = even + odd;
                values[index + j + length / 2] = even - odd;
                w *= wlen;
            }
        }
    }
}