#include "owalkie/link_signal.hpp"
#include "owalkie_core.h"

#include <iostream>

#define EXPECT_EQ(a, b)                                                                               \
    do {                                                                                              \
        if ((a) != (b)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected " << (b) << " got " << (a) << "\n"; \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_link_signal_tests() {
    auto& reg = owalkie::link_signal::Registry::instance();
    reg.clear(owalkie::link_signal::Mode::Wifi);
    reg.clear(owalkie::link_signal::Mode::Cell);
    EXPECT_EQ(owalkie_get_uplink_signal_byte(), 255);

    EXPECT_EQ(owalkie::link_signal::wifiRssiToByte(-100), 0);
    EXPECT_EQ(owalkie::link_signal::wifiRssiToByte(-55), 255);
    EXPECT_EQ(owalkie::link_signal::cellLevelToByte(0), 0);
    EXPECT_EQ(owalkie::link_signal::cellLevelToByte(4), 255);

    EXPECT_EQ(owalkie_report_signal(OWALKIE_SIGNAL_WIFI, -80), OWALKIE_OK);
    EXPECT_EQ(owalkie_report_signal(OWALKIE_SIGNAL_CELL, 2), OWALKIE_OK);
    const int combined = owalkie_get_uplink_signal_byte();
    if (combined <= 0 || combined >= 254) {
        std::cerr << "unexpected combined byte " << combined << "\n";
        return 1;
    }

    EXPECT_EQ(owalkie_report_signal(OWALKIE_SIGNAL_CELL, 9), OWALKIE_ERR_INVALID_ARG);
    EXPECT_EQ(owalkie_clear_signal(OWALKIE_SIGNAL_WIFI), OWALKIE_OK);
    EXPECT_EQ(owalkie_get_uplink_signal_byte(), owalkie::link_signal::cellLevelToByte(2));

    reg.clear(owalkie::link_signal::Mode::Wifi);
    reg.clear(owalkie::link_signal::Mode::Cell);
    return 0;
}
