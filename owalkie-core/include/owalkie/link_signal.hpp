#pragma once

#include <mutex>

#include "owalkie/types.hpp"

namespace owalkie::link_signal {

enum class Mode {
    Wifi,
    Cell,
};

/** RSSI in dBm (Wi‑Fi) → 0..255 (Android WifiManager.calculateSignalLevel(rssi, 256)). */
int wifiRssiToByte(int rssiDbm);

/** Cellular level 0..4 → 0..255. */
int cellLevelToByte(int level0to4);

/** Global uplink signal registry (applies to all active sessions). */
class Registry {
public:
    static Registry& instance();

    Result report(Mode mode, int value);
    Result clear(Mode mode);

    /** Combined byte for UDP audio frames: max(wifi, cell), default 255 if none reported. */
    int combinedByte() const;

private:
    Registry() = default;

    mutable std::mutex mu_;
    bool wifiValid_ = false;
    int wifiByte_ = 0;
    bool cellValid_ = false;
    int cellByte_ = 0;
};

} // namespace owalkie::link_signal
