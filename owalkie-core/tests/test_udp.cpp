#include "owalkie_core.h"

#include <cstring>
#include <iostream>

#define EXPECT_EQ(a, b)                                                                               \
    do {                                                                                              \
        if ((a) != (b)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected " << (b) << " got " << (a) << "\n"; \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

#define EXPECT_TRUE(x)                                                                                \
    do {                                                                                              \
        if (!(x)) {                                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected true\n";                          \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_udp_tests() {
    const uint8_t opus[] = {0x01, 0x02, 0x03};
    owalkie_udp_audio_packet in{};
    in.session_id = 0x01020304;
    in.sequence = 7;
    in.signal_strength = 255;
    in.opus = opus;
    in.opus_len = sizeof(opus);

    uint8_t buf[64]{};
    size_t len = 0;
    EXPECT_EQ(owalkie_udp_pack(&in, buf, sizeof(buf), &len), OWALKIE_OK);
    EXPECT_EQ(len, 9 + sizeof(opus));

    owalkie_udp_audio_packet out{};
    EXPECT_EQ(owalkie_udp_unpack(buf, len, &out), OWALKIE_OK);
    EXPECT_EQ(out.session_id, in.session_id);
    EXPECT_EQ(out.sequence, in.sequence);
    EXPECT_EQ(out.signal_strength, in.signal_strength);
    EXPECT_EQ(out.opus_len, sizeof(opus));
    EXPECT_TRUE(out.opus != nullptr);
    EXPECT_EQ(std::memcmp(out.opus, opus, sizeof(opus)), 0);

    uint8_t keepalive[9] = {1, 2, 3, 4, 0, 0, 0, 0, 255};
    EXPECT_TRUE(owalkie_udp_is_keepalive_signal(keepalive, sizeof(keepalive)) == 1);

    uint8_t ack[9] = {1, 2, 3, 4, 0, 0, 0, 0, 254};
    EXPECT_TRUE(owalkie_udp_is_keepalive_ack(ack, sizeof(ack), 0x01020304) == 1);

    uint8_t eof[9] = {1, 2, 3, 4, 0, 0, 0, 9, 0};
    EXPECT_TRUE(owalkie_udp_is_tx_eof_marker(eof, sizeof(eof)) == 1);

    return 0;
}
