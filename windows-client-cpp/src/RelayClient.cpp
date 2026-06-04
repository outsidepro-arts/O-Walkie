#include "RelayClient.h"

#include "owalkie/client_events.hpp"

#include <algorithm>
#include <atomic>

RelayClient::RelayClient() = default;

RelayClient::~RelayClient() {
    Disconnect(false);
}

WelcomeConfig RelayClient::MapWelcome(const owalkie::WelcomeConfig& src) {
    WelcomeConfig out{};
    out.sessionId = src.sessionId;
    out.sampleRate = src.sampleRate;
    out.packetMs = src.packetMs;
    out.bitrate = src.opus.bitrate;
    out.complexity = src.opus.complexity;
    out.fec = src.opus.fec;
    out.dtx = src.opus.dtx;
    out.application = src.opus.application;
    out.busyMode = src.busyMode;
    out.busyTimeoutSec = 0;
    out.transmitTimeoutSec = src.transmitTimeoutSec;
    return out;
}

void RelayClient::clearManagedSession() {
    sessionId_ = owalkie::kInvalidSessionId;
}

void RelayClient::PostConnectionLostOnce() {
    if (!autoReconnectDesired_.load()) {
        return;
    }
    bool expected = false;
    if (connectionLostPosted_.compare_exchange_strong(expected, true)) {
        if (onConnectionLost_) {
            onConnectionLost_();
        }
    }
}

void RelayClient::HandleSessionEvent(const owalkie::Event& event) {
    if (!owalkie::client_events::isVisible(event.type)) {
        return;
    }
    switch (event.type) {
        case owalkie::EventType::SessionReady: {
            connectionLostPosted_.store(false);
            const WelcomeConfig cfg = MapWelcome(event.welcome);
            {
                std::lock_guard<std::mutex> lg(stateMu_);
                cfg_ = cfg;
            }
            if (onWelcome_) {
                onWelcome_(cfg);
            }
            if (onConnected_) {
                onConnected_(true);
            }
            if (onStatus_) {
                onStatus_("connected");
            }
            break;
        }
        case owalkie::EventType::ConnectFailed:
            clearManagedSession();
            if (notifyUiCallbacks_.load(std::memory_order_relaxed)) {
                if (onConnected_) {
                    onConnected_(false);
                }
                if (!event.disconnectReason.empty() && onStatus_) {
                    onStatus_(std::string("connect failed: ") + event.disconnectReason);
                }
                PostConnectionLostOnce();
            }
            break;
        case owalkie::EventType::Disconnected:
            clearManagedSession();
            if (notifyUiCallbacks_.load(std::memory_order_relaxed)) {
                if (onConnected_) {
                    onConnected_(false);
                }
                if (!event.disconnectReason.empty() && onStatus_) {
                    onStatus_(std::string("ws ended: ") + event.disconnectReason);
                }
                PostConnectionLostOnce();
            }
            break;
        case owalkie::EventType::ProtocolError:
            autoReconnectDesired_.store(false);
            clearManagedSession();
            if (notifyUiCallbacks_.load(std::memory_order_relaxed)) {
                if (onStatus_) {
                    onStatus_(event.protocolError.empty() ? "Protocol mismatch" : event.protocolError);
                }
                if (onConnected_) {
                    onConnected_(false);
                }
            }
            break;
        case owalkie::EventType::RxBroadcastStart:
            if (onRxBroadcastStart_) {
                onRxBroadcastStart_(event.rxBusyMode);
            }
            break;
        case owalkie::EventType::RxBroadcastEnd:
            if (onRxBroadcastEnd_) {
                onRxBroadcastEnd_();
            }
            break;
        case owalkie::EventType::PttLocked:
            if (onServerPttLock_) {
                onServerPttLock_(event.pttDisplaySec);
            }
            break;
        case owalkie::EventType::PttUnlocked:
            if (onServerPttUnlock_) {
                onServerPttUnlock_();
            }
            break;
        case owalkie::EventType::TxCountdownStart:
            if (onTxCountdownStart_) {
                onTxCountdownStart_();
            }
            break;
        case owalkie::EventType::TxStop:
            if (onTxStop_) {
                onTxStop_();
            }
            break;
        default:
            break;
    }
}

bool RelayClient::Connect(const std::string& host, int serverPort, const std::string& channel, bool repeater) {
    auto& manager = owalkie::SessionManager::instance();
    if (sessionId_ != owalkie::kInvalidSessionId && manager.isSessionReady(sessionId_)) {
        return true;
    }

    Disconnect(false);

    autoReconnectDesired_.store(true);
    connectionLostPosted_.store(false);

    owalkie::SessionCallbacks callbacks{};
    callbacks.onRxPcm = [this](std::span<const int16_t> pcm, int sampleRate, int packetMs) {
        (void)sampleRate;
        (void)packetMs;
        if (!onPcmFrame_ || pcm.empty()) {
            return;
        }
        onPcmFrame_(pcm.data(), pcm.size());
    };
    callbacks.onSessionEvent = [this](const owalkie::Event& event) {
        HandleSessionEvent(event);
    };

    owalkie::ConnectParams params{};
    params.host = host;
    params.port = serverPort;
    params.channel = channel.empty() ? "global" : channel;
    params.repeaterMode = repeater;

    const owalkie::SessionId id = manager.connect(params, std::move(callbacks));
    if (id == owalkie::kInvalidSessionId) {
        return false;
    }

    sessionId_ = id;
    manager.setPowerProfile(sessionId_, owalkie::PowerProfile::Foreground);
    return true;
}

void RelayClient::Disconnect(bool notifyCallbacks) {
    notifyUiCallbacks_.store(notifyCallbacks, std::memory_order_relaxed);
    autoReconnectDesired_.store(false);
    connectionLostPosted_.store(false);
    if (sessionId_ != owalkie::kInvalidSessionId) {
        const owalkie::SessionId id = sessionId_;
        sessionId_ = owalkie::kInvalidSessionId;
        // Always async halt; never block UI on disconnectInternal()/thread joins.
        owalkie::SessionManager::instance().haltAndRelease(id);
    }
    if (notifyCallbacks && onConnected_) {
        onConnected_(false);
    }
}

void RelayClient::JoinWorkerThreads() {
    // Threads are owned by owalkie-core session manager.
}

bool RelayClient::IsConnected() const {
    return sessionId_ != owalkie::kInvalidSessionId &&
        owalkie::SessionManager::instance().isSessionReady(sessionId_);
}

bool RelayClient::AutoReconnectDesired() const {
    return autoReconnectDesired_.load();
}

WelcomeConfig RelayClient::CurrentConfig() const {
    std::lock_guard<std::mutex> lg(stateMu_);
    return cfg_;
}

bool RelayClient::TxStart() {
    if (!IsConnected()) {
        return false;
    }
    return owalkie::SessionManager::instance().txStart(sessionId_) == owalkie::Result::Ok;
}

void RelayClient::PushTxPcm(const int16_t* samples, size_t count) {
    if (!IsConnected() || !samples || count == 0) {
        return;
    }
    (void)owalkie::SessionManager::instance().pushTxPcm(
        sessionId_,
        std::span<const int16_t>(samples, count));
}

void RelayClient::TxEnd() {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().txEnd(sessionId_);
}

void RelayClient::SetRepeaterMode(bool enabled) {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().setRepeaterMode(sessionId_, enabled);
}
