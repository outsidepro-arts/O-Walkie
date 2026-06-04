#pragma once

#include "owalkie/types.hpp"
#include "owalkie_core.h"

namespace owalkie::client_events {

/** Whether this internal session event is forwarded to managed / JNI / app callbacks. */
inline bool isVisible(EventType type) {
    switch (type) {
        case EventType::SessionReady:
        case EventType::Disconnected:
        case EventType::ProtocolError:
        case EventType::ConnectFailed:
        case EventType::RxBroadcastStart:
        case EventType::RxBroadcastEnd:
        case EventType::PttLocked:
        case EventType::PttUnlocked:
        case EventType::TxCountdownStart:
        case EventType::TxStop:
            return true;
        default:
            return false;
    }
}

/** Maps internal EventType to the public owalkie_event_type (only for visible events). */
inline owalkie_event_type toPublic(EventType type) {
    switch (type) {
        case EventType::SessionReady:
            return OWALKIE_EV_READY;
        case EventType::Disconnected:
            return OWALKIE_EV_DISCONNECTED;
        case EventType::ProtocolError:
            return OWALKIE_EV_PROTOCOL_ERROR;
        case EventType::ConnectFailed:
            return OWALKIE_EV_CONNECT_FAILED;
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
        default:
            return OWALKIE_EV_READY;
    }
}

} // namespace owalkie::client_events
