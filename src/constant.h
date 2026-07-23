#pragma once
#include <cstdint>
namespace constant {
constexpr uint32_t MAGIC_NUMBER = 0x484D4654u;
enum BW {
    WSCAN_BW_200M = 0,
    WSCAN_BW_100M = 1,
    WSCAN_BW_20M  = 2,
    WSCAN_BW_10M  = 3,
    WSCAN_BW_5M   = 4,
};
}  // namespace constant