#include "owalkie_core.h"

#include <cstring>
#include <iostream>
#include <string>

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

int run_json_tests() {
    const char* welcome =
        R"({"type":"welcome","sessionId":42,"packetMs":20,"sampleRate":8000,"opus":{"bitrate":12000,"complexity":5,"fec":true,"dtx":false,"application":"voip"},"protocolVersion":2,"busyMode":true,"transmitTimeoutSec":60})";

    owalkie_welcome_config cfg{};
    char opusApp[32]{};
    EXPECT_EQ(
        owalkie_json_parse_welcome(welcome, 0, &cfg, opusApp, sizeof(opusApp)),
        OWALKIE_OK);
    EXPECT_EQ(cfg.session_id, 42u);
    EXPECT_EQ(cfg.sample_rate, 8000);
    EXPECT_EQ(cfg.packet_ms, 20);
    EXPECT_EQ(cfg.protocol_version, 2);
    EXPECT_EQ(cfg.busy_mode, 1);
    EXPECT_EQ(cfg.transmit_timeout_sec, 60);
    EXPECT_EQ(std::strcmp(opusApp, "voip"), 0);

    owalkie_event ev{};
    EXPECT_EQ(
        owalkie_json_parse_server_message(welcome, 0, &ev, opusApp, sizeof(opusApp)),
        OWALKIE_OK);
    EXPECT_EQ(ev.type, OWALKIE_EV_WELCOME);
    EXPECT_EQ(ev.u.welcome.config.session_id, 42u);

    const char* pttLock = R"({"type":"ptt_lock","displaySec":3})";
    EXPECT_EQ(owalkie_json_parse_server_message(pttLock, 0, &ev, nullptr, 0), OWALKIE_OK);
    EXPECT_EQ(ev.type, OWALKIE_EV_PTT_LOCKED);
    EXPECT_EQ(ev.u.ptt_locked.display_sec, 3);

    char buf[128]{};
    size_t written = 0;
    EXPECT_EQ(owalkie_json_build_join("test", buf, sizeof(buf), &written), OWALKIE_OK);
    EXPECT_TRUE(std::string(buf).find("\"channel\":\"test\"") != std::string::npos);

    EXPECT_EQ(owalkie_json_build_udp_hello(12345, buf, sizeof(buf), &written), OWALKIE_OK);
    EXPECT_TRUE(std::string(buf).find("\"udpPort\":12345") != std::string::npos);

    EXPECT_EQ(owalkie_json_build_repeater_mode(1, buf, sizeof(buf), &written), OWALKIE_OK);
    EXPECT_TRUE(std::string(buf).find("\"enabled\":true") != std::string::npos);

    return 0;
}
