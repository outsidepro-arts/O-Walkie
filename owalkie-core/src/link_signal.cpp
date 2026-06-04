#include "owalkie/link_signal.hpp"

#include "owalkie/udp.hpp"

#include <algorithm>

namespace owalkie::link_signal {
namespace {

constexpr int kWifiMinRssiDbm = -100;
constexpr int kWifiMaxRssiDbm = -55;
constexpr int kWifiLevels = 256;

int clampByte(int value) {
    return std::clamp(value, 0, 255);
}

} // namespace

int wifiRssiToByte(int rssiDbm) {
    if (rssiDbm <= kWifiMinRssiDbm) {
        return 0;
    }
    if (rssiDbm >= kWifiMaxRssiDbm) {
        return kWifiLevels - 1;
    }
    const float numer = static_cast<float>(rssiDbm - kWifiMinRssiDbm);
    const float denom = static_cast<float>(kWifiMaxRssiDbm - kWifiMinRssiDbm);
    return clampByte(static_cast<int>((numer * static_cast<float>(kWifiLevels - 1)) / denom));
}

int cellLevelToByte(int level0to4) {
    const int level = std::clamp(level0to4, 0, 4);
    return clampByte((level * 255) / 4);
}

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

Result Registry::report(Mode mode, int value) {
    std::lock_guard<std::mutex> lock(mu_);
    switch (mode) {
        case Mode::Wifi: {
            wifiByte_ = wifiRssiToByte(value);
            wifiValid_ = true;
            return Result::Ok;
        }
        case Mode::Cell: {
            if (value < 0 || value > 4) {
                return Result::InvalidArg;
            }
            cellByte_ = cellLevelToByte(value);
            cellValid_ = true;
            return Result::Ok;
        }
    }
    return Result::InvalidArg;
}

Result Registry::clear(Mode mode) {
    std::lock_guard<std::mutex> lock(mu_);
    switch (mode) {
        case Mode::Wifi:
            wifiValid_ = false;
            wifiByte_ = 0;
            return Result::Ok;
        case Mode::Cell:
            cellValid_ = false;
            cellByte_ = 0;
            return Result::Ok;
    }
    return Result::InvalidArg;
}

int Registry::combinedByte() const {
    std::lock_guard<std::mutex> lock(mu_);
    int combined = 0;
    bool any = false;
    if (wifiValid_) {
        combined = wifiByte_;
        any = true;
    }
    if (cellValid_) {
        combined = any ? std::max(combined, cellByte_) : cellByte_;
        any = true;
    }
    if (!any) {
        return static_cast<int>(pkt::kDefaultTxSignalStrength);
    }
    if (combined == static_cast<int>(pkt::kKeepaliveAckSignal)) {
        return static_cast<int>(pkt::kDefaultTxSignalStrength);
    }
    return combined;
}

} // namespace owalkie::link_signal
