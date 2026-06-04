#include "owalkie/json.hpp"

#include <iostream>
#include <string>

#define EXPECT_TRUE(x)                                                                                \
    do {                                                                                              \
        if (!(x)) {                                                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected true\n";                           \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_json_wire_tests() {
    EXPECT_TRUE(owalkie::json::buildJoin("test").find("\"channel\":\"test\"") != std::string::npos);
    EXPECT_TRUE(owalkie::json::buildUdpHello(12345).find("\"udpPort\":12345") != std::string::npos);
    EXPECT_TRUE(owalkie::json::buildRepeaterMode(true).find("\"enabled\":true") != std::string::npos);
    EXPECT_TRUE(owalkie::json::buildHeartbeat().find("\"type\":\"heartbeat\"") != std::string::npos);
    return 0;
}
