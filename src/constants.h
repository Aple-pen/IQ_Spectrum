#pragma once
#include <array>
namespace constants::WB {
constexpr std::array<float, 5> FSYMBOL = {245.76e6, 122.88e6, 30.72e6, 15.36e6,
                                          7.68e6};
constexpr std::array<uint32_t, 5> BANDWIDTH = {200000000, 100000000, 20000000,
                                               10000000, 5000000};
constexpr int32_t EFFECTIVE_BIN = 1666; // 1,666.6666666666666666666666666667
constexpr std::array<int32_t, 5> BW_KHz = {200000, 100000, 24000, 12000, 6000};

const char *BwLabel[5] = {"200 MHz", "100 MHz", "24 MHz", "12 MHz", "6 MHz"};
}; // namespace constants::WB