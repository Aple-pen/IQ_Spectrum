#pragma once

#include <complex>
#include <vector>

class Fft {
public:
    static bool IsPowerOfTwo(int value);
    static std::vector<float> MagnitudeSpectrum(const std::vector<float>& samples, int fftSize, float sampleRateHz);

private:
    static void Transform(std::vector<std::complex<float>>& values);
};