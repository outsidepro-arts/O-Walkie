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

int run_session_info_tests() {
    owalkie_session_info info{};
    EXPECT_EQ(
        owalkie_get_session_info(owalkie_invalid_session_id(), &info, nullptr, 0),
        OWALKIE_ERR_INVALID_ARG);
    EXPECT_EQ(owalkie_get_session_info(999, &info, nullptr, 0), OWALKIE_ERR_INVALID_ARG);
    EXPECT_EQ(owalkie_get_session_info(999, nullptr, nullptr, 0), OWALKIE_ERR_INVALID_ARG);
    return 0;
}
