#include "owalkie_core.h"

#include <iostream>

#define EXPECT_TRUE(x)                                                                                \
    do {                                                                                              \
        if (!(x)) {                                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected true\n";                          \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_signal_tests() {
    const owalkie_signal_point points[] = {
        {880.0, 100},
        {0.0, 50},
    };
    owalkie_signal_pattern pattern{};
    pattern.points = points;
    pattern.point_count = 2;
    pattern.tail_ms = 40;
    pattern.repeat_count = 1;
    pattern.gain = 0.26;

    int16_t* samples = nullptr;
    size_t count = 0;
    EXPECT_TRUE(owalkie_signal_generate_pcm(&pattern, 8000, &samples, &count) == OWALKIE_OK);
    EXPECT_TRUE(samples != nullptr);
    EXPECT_TRUE(count > 0);
    const size_t expectedMin = static_cast<size_t>((8000 * 100) / 1000 + (8000 * 50) / 1000 + (8000 * 40) / 1000);
    EXPECT_TRUE(count >= expectedMin);
    owalkie_signal_free_pcm(samples);
    return 0;
}
