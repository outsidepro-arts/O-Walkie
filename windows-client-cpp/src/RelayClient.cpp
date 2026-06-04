#include "RelayClient.h"

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
    switch (event.type) {
        case owalkie::EventType::Connecting:
            if (onStatus_) {
                onStatus_("Connecting");
            }
            break;
        case owalkie::EventType::SessionReady: {
            connectionLostPosted_.store(false);
            const WelcomeConfig cfg = MapWelcome(event.welcome);
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
        case owalkie::EventType::Welcome: {
            const WelcomeConfig local = MapWelcome(event.welcome);
            {
                std::lock_guard<std::mutex> lg(stateMu_);
                cfg_ = local;
            }
            if (onWelcome_) {
                onWelcome_(local);
            }
            if (onStatus_) {
                onStatus_("Welcome received");
            }
            break;
        }
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
        case owalkie::EventType::UdpTransportLost:
            if (onStatus_) {
                onStatus_(event.disconnectReason.empty()
                    ? "UDP transport lost"
                    : std::string("UDP transport lost: ") + event.disconnectReason);
            }
            break;
        case owalkie::EventType::UdpTransportReady:
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
    callbacks.onRxOpus = [this](std::span<const uint8_t> opus) {
        if (!onOpusFrame_ || opus.empty()) {
            return;
        }
        onOpusFrame_(std::vector<uint8_t>(opus.begin(), opus.end()));
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

void RelayClient::SendOpusFrame(const uint8_t* data, size_t size, uint8_t signal) {
    if (!IsConnected() || !data || size == 0) {
        return;
    }
    (void)owalkie::SessionManager::instance().sendTxOpus(
        sessionId_,
        std::span<const uint8_t>(data, size),
        static_cast<int>(signal));
}

void RelayClient::SendTxEofBurst() {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().sendTxEofBurst(sessionId_);
}

void RelayClient::SetRepeaterMode(bool enabled) {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().setRepeaterMode(sessionId_, enabled);
}
