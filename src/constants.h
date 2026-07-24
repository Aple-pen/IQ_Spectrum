#pragma once
#include <array>
namespace constants::WB {
constexpr std::array<float, 5> FSYMBOL = {245.76e6, 122.88e6, 30.72e6, 15.36e6,
                                          7.68e6};
constexpr std::array<int32_t, 5> BANDWIDTH = {200e6, 100e6, 20e6, 10e6, 5e6};
constexpr int32_t EFFECTIVE_BIN = 1666; // 1,666.6666666666666666666666666667

}; // namespace constants::WB