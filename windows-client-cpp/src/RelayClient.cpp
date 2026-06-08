#include "RelayClient.h"

#include "owalkie/client_events.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

RelayClient::RelayClient() = default;

RelayClient::~RelayClient() {
    StopReconnectLoop();
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

void RelayClient::HandleSessionEvent(const owalkie::Event& event) {
    if (!owalkie::client_events::isVisible(event.type)) {
        return;
    }
    switch (event.type) {
        case owalkie::EventType::ConnectionLost:
            if (onStatus_) {
                onStatus_(event.disconnectReason.empty() ? "reconnecting" : event.disconnectReason);
            }
            break;
        case owalkie::EventType::Connected: {
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
        case owalkie::EventType::ConnectionFailed:
            StopReconnectLoop();
            clearManagedSession();
            if (notifyUiCallbacks_.load(std::memory_order_relaxed)) {
                if (onConnected_) {
                    onConnected_(false);
                }
                if (!event.disconnectReason.empty() && onStatus_) {
                    onStatus_(std::string("connect failed: ") + event.disconnectReason);
                }
            }
            break;
        case owalkie::EventType::Disconnected:
            StopReconnectLoop();
            clearManagedSession();
            if (notifyUiCallbacks_.load(std::memory_order_relaxed)) {
                if (onConnected_) {
                    onConnected_(false);
                }
                if (!event.disconnectReason.empty() && onStatus_) {
                    onStatus_(std::string("ws ended: ") + event.disconnectReason);
                }
            }
            break;
        case owalkie::EventType::ProtocolError:
            StopReconnectLoop();
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

    Disconnect(false);

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

    const owalkie::SessionId id = manager.prepareConnection(params, std::move(callbacks));
    if (id == owalkie::kInvalidSessionId) {
        return false;
    }

    sessionId_ = id;
    notifyUiCallbacks_.store(true, std::memory_order_relaxed);
    manager.setPowerProfile(sessionId_, owalkie::PowerProfile::Foreground);
    StartReconnectLoop();
    (void)TryReconnect(10000);
    return true;
}

void RelayClient::DetachUiCallbacks() {
    onStatus_ = {};
    onConnected_ = {};
    onWelcome_ = {};
    onPcmFrame_ = {};
    onTxCountdownStart_ = {};
    onServerPttLock_ = {};
    onServerPttUnlock_ = {};
    onRxBroadcastStart_ = {};
    onRxBroadcastEnd_ = {};
    onTxStop_ = {};
}

bool RelayClient::TryReconnect(int timeoutMs) {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return false;
    }
    if (connectInFlight_.exchange(true)) {
        return IsConnected();
    }
    struct ConnectGuard {
        std::atomic<bool>& flag;
        ~ConnectGuard() { flag.store(false); }
    } guard{connectInFlight_};

    auto& manager = owalkie::SessionManager::instance();
    if (!manager.isValid(sessionId_)) {
        return false;
    }
    const owalkie::Result result = manager.connect(sessionId_, timeoutMs);
    return result == owalkie::Result::Ok && manager.isSessionReady(sessionId_);
}

void RelayClient::StartReconnectLoop() {
    if (reconnectLoopRunning_.exchange(true)) {
        return;
    }
    reconnectBackoffMs_ = 1500;
    reconnectThread_ = std::thread([this]() {
        while (reconnectLoopRunning_.load(std::memory_order_relaxed)) {
            if (sessionId_ == owalkie::kInvalidSessionId) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            owalkie::SessionState st{};
            bool ready = false;
            const auto infoResult = owalkie::SessionManager::instance().getSessionInfo(sessionId_, &st, &ready);
            const bool transportUp = infoResult == owalkie::Result::Ok && st.connected;
            if (IsConnected() || transportUp) {
                reconnectBackoffMs_ = 1500;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            if (connectInFlight_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            TryReconnect(3500);
            const int backoff = reconnectBackoffMs_;
            reconnectBackoffMs_ = std::min(reconnectBackoffMs_ * 2, 30000);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        }
    });
}

void RelayClient::StopReconnectLoop() {
    if (!reconnectLoopRunning_.exchange(false)) {
        return;
    }
    if (reconnectThread_.joinable()) {
        reconnectThread_.join();
    }
}

void RelayClient::Disconnect(bool notifyCallbacks) {
    StopReconnectLoop();
    notifyUiCallbacks_.store(notifyCallbacks, std::memory_order_relaxed);
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
        owalkie::SessionManager::instance().isValid(sessionId_) &&
        owalkie::SessionManager::instance().isSessionReady(sessionId_);
}

WelcomeConfig RelayClient::CurrentConfig() const {
    std::lock_guard<std::mutex> lg(stateMu_);
    return cfg_;
}

bool RelayClient::TxOpen() {
    if (!IsConnected()) {
        return false;
    }
    return owalkie::SessionManager::instance().submitTx(sessionId_, owalkie::TxSubmitOp::Open) ==
        owalkie::Result::Ok;
}

void RelayClient::TxPcm(const int16_t* samples, size_t count) {
    if (!IsConnected() || !samples || count == 0) {
        return;
    }
    (void)owalkie::SessionManager::instance().submitTx(
        sessionId_,
        owalkie::TxSubmitOp::Pcm,
        std::span<const int16_t>(samples, count));
}

void RelayClient::TxClose() {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().submitTx(sessionId_, owalkie::TxSubmitOp::Close);
}

void RelayClient::TxAbort() {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().submitTx(sessionId_, owalkie::TxSubmitOp::Abort);
}

void RelayClient::SetRepeaterMode(bool enabled) {
    if (sessionId_ == owalkie::kInvalidSessionId) {
        return;
    }
    (void)owalkie::SessionManager::instance().setRepeaterMode(sessionId_, enabled);
}
