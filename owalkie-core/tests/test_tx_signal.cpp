#include "owalkie_core.h"

#include <iostream>

#define EXPECT_EQ(a, b)                                                                               \
    do {                                                                                              \
        if ((a) != (b)) {                                                                             \
            std::cerr << __FILE__ << ":" << __LINE__ << " expected " << (b) << " got " << (a) << "\n"; \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

int run_tx_signal_tests() {
#ifdef OWALKIE_CORE_HAS_SESSION
    owalkie_session_t* session = nullptr;
    EXPECT_EQ(owalkie_session_create(&session), OWALKIE_OK);
    EXPECT_EQ(owalkie_session_get_tx_signal_strength(session), 255);
    EXPECT_EQ(owalkie_session_set_tx_signal_strength(session, 128), OWALKIE_OK);
    EXPECT_EQ(owalkie_session_get_tx_signal_strength(session), 128);
    EXPECT_EQ(owalkie_session_set_tx_signal_strength(session, 254), OWALKIE_ERR_INVALID_ARG);
    EXPECT_EQ(owalkie_session_get_tx_signal_strength(session), 128);
    EXPECT_EQ(owalkie_session_set_tx_signal_strength(session, 255), OWALKIE_OK);
    EXPECT_EQ(owalkie_session_set_tx_signal_strength(session, -1), OWALKIE_ERR_INVALID_ARG);
    EXPECT_EQ(owalkie_session_set_tx_signal_strength(session, 256), OWALKIE_ERR_INVALID_ARG);
    owalkie_session_destroy(session);
#else
    EXPECT_EQ(owalkie_session_set_tx_signal_strength(nullptr, 100), OWALKIE_ERR_UNSUPPORTED);
#endif
    return 0;
}
