#pragma once

#include <complex>
#include <vector>

class Fft {
public:
    static bool IsPowerOfTwo(int value);
    // Real-input: returns N/2 bins, frequencies 0..fs/2
    static std::vector<float> MagnitudeSpectrum(const std::vector<float>& samples, int fftSize, float sampleRateHz);
    // Complex IQ input: returns N bins (fftshifted), frequencies -fs/2..+fs/2
    // freqOffsetHz shifts spectrum center by applying complex rotation before FFT
    static std::vector<float> MagnitudeSpectrumIQ(const std::vector<float>& iSamples, const std::vector<float>& qSamples, int fftSize, float sampleRateHz, float freqOffsetHz = 0.0f);

private:
    static void Transform(std::vector<std::complex<float>>& values);
};