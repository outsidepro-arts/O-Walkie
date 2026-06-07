#include "owalkie/client_events.hpp"
#include "owalkie_core.h"

#include <iostream>

#define EXPECT_TRUE(x)                                                                                \
    do {                                                                                              \
        if (!(x)) {                                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected true\n";                          \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

#define EXPECT_EQ(a, b)                                                                               \
    do {                                                                                              \
        if ((a) != (b)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected " << (b) << " got " << (a) << "\n"; \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_client_events_tests() {
    EXPECT_TRUE(owalkie::client_events::isVisible(owalkie::EventType::ConnectionLost));
    EXPECT_EQ(
        owalkie::client_events::toPublic(owalkie::EventType::ConnectionLost),
        OWALKIE_EV_CONNECTION_LOST);
    EXPECT_TRUE(!owalkie::client_events::isVisible(owalkie::EventType::UdpTransportLost));
    return 0;
}
