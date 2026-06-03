#include "owalkie_core.h"

#include <iostream>

#define EXPECT_EQ(a, b)                                                                               \
    do {                                                                                              \
        if ((a) != (b)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected " << (b) << " got " << (a) << "\n"; \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_protocol_tests() {
    EXPECT_EQ(owalkie_normalize_sample_rate(8000), 8000);
    EXPECT_EQ(owalkie_normalize_sample_rate(9999), 8000);
    EXPECT_EQ(owalkie_normalize_packet_ms(20), 20);
    EXPECT_EQ(owalkie_normalize_packet_ms(15), 20);
    EXPECT_EQ(owalkie_frame_samples(8000, 20), 160);
    EXPECT_EQ(owalkie_frame_samples(48000, 40), 1920);
    EXPECT_EQ(owalkie_version_major(), 0);
    EXPECT_EQ(owalkie_version_minor(), 1);
    return 0;
}
