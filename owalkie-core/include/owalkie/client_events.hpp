#pragma once

#include "owalkie/types.hpp"
#include "owalkie_core.h"

namespace owalkie::client_events {

/** Whether this internal session event is forwarded to managed / JNI / app callbacks. */
inline bool isVisible(EventType type) {
    switch (type) {
        case EventType::Connected:
        case EventType::Disconnected:
        case EventType::ProtocolError:
        case EventType::ConnectionFailed:
        case EventType::RxBroadcastStart:
        case EventType::RxBroadcastEnd:
        case EventType::PttLocked:
        case EventType::PttUnlocked:
        case EventType::TxCountdownStart:
        case EventType::TxStop:
        case EventType::ConnectionLost:
            return true;
        default:
            return false;
    }
}

/** Maps internal EventType to the public owalkie_event_type (only for visible events). */
inline owalkie_event_type toPublic(EventType type) {
    switch (type) {
        case EventType::Connected:
            return OWALKIE_EV_CONNECTED;
        case EventType::Disconnected:
            return OWALKIE_EV_DISCONNECTED;
        case EventType::ProtocolError:
            return OWALKIE_EV_PROTOCOL_ERROR;
        case EventType::ConnectionFailed:
            return OWALKIE_EV_CONNECTION_FAILED;
        case EventType::RxBroadcastStart:
            return OWALKIE_EV_RX_BROADCAST_START;
        case EventType::RxBroadcastEnd:
            return OWALKIE_EV_RX_BROADCAST_END;
        case EventType::PttLocked:
            return OWALKIE_EV_PTT_LOCKED;
        case EventType::PttUnlocked:
            return OWALKIE_EV_PTT_UNLOCKED;
        case EventType::TxCountdownStart:
            return OWALKIE_EV_TX_COUNTDOWN_START;
        case EventType::TxStop:
            return OWALKIE_EV_TX_STOP;
        case EventType::ConnectionLost:
            return OWALKIE_EV_CONNECTION_LOST;
        default:
            return OWALKIE_EV_CONNECTED;
    }
}

} // namespace owalkie::client_events
