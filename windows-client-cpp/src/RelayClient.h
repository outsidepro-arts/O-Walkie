#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "owalkie/session_manager.hpp"

struct WelcomeConfig {
    uint32_t sessionId = 0;
    int sampleRate = 8000;
    int packetMs = 20;
    int bitrate = 12000;
    int complexity = 5;
    bool fec = true;
    bool dtx = false;
    std::string application = "voip";
    bool busyMode = false;
    int busyTimeoutSec = 0;
    int transmitTimeoutSec = 60;
};

class RelayClient {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using ConnectedCallback = std::function<void(bool)>;
    using WelcomeCallback = std::function<void(const WelcomeConfig&)>;
    using OpusFrameCallback = std::function<void(const std::vector<uint8_t>&)>;
    using TxCountdownStartCallback = std::function<void()>;
    using ServerPttLockCallback = std::function<void(int displaySec)>;
    using ServerPttUnlockCallback = std::function<void()>;
    using RxBroadcastStartCallback = std::function<void(bool busyMode)>;
    using RxBroadcastEndCallback = std::function<void()>;
    using TxStopCallback = std::function<void()>;
    using ConnectionLostCallback = std::function<void()>;

    RelayClient();
    ~RelayClient();

    bool Connect(const std::string& host, int serverPort, const std::string& channel, bool repeater);
    void Disconnect(bool notifyCallbacks = true);
    void JoinWorkerThreads();

    bool IsConnected() const;
    bool AutoReconnectDesired() const;
    WelcomeConfig CurrentConfig() const;

    void SendOpusFrame(const uint8_t* data, size_t size, uint8_t signal);
    void SendTxEofBurst();
    void SetRepeaterMode(bool enabled);

    void SetStatusCallback(StatusCallback cb) { onStatus_ = std::move(cb); }
    void SetConnectedCallback(ConnectedCallback cb) { onConnected_ = std::move(cb); }
    void SetWelcomeCallback(WelcomeCallback cb) { onWelcome_ = std::move(cb); }
    void SetOpusFrameCallback(OpusFrameCallback cb) { onOpusFrame_ = std::move(cb); }
    void SetTxCountdownStartCallback(TxCountdownStartCallback cb) { onTxCountdownStart_ = std::move(cb); }
    void SetServerPttLockCallback(ServerPttLockCallback cb) { onServerPttLock_ = std::move(cb); }
    void SetServerPttUnlockCallback(ServerPttUnlockCallback cb) { onServerPttUnlock_ = std::move(cb); }
    void SetRxBroadcastStartCallback(RxBroadcastStartCallback cb) { onRxBroadcastStart_ = std::move(cb); }
    void SetRxBroadcastEndCallback(RxBroadcastEndCallback cb) { onRxBroadcastEnd_ = std::move(cb); }
    void SetTxStopCallback(TxStopCallback cb) { onTxStop_ = std::move(cb); }
    void SetConnectionLostCallback(ConnectionLostCallback cb) { onConnectionLost_ = std::move(cb); }

private:
    static WelcomeConfig MapWelcome(const owalkie::WelcomeConfig& src);
    void HandleSessionEvent(const owalkie::Event& event);
    void PostConnectionLostOnce();
    void clearManagedSession();

    owalkie::SessionId sessionId_ = owalkie::kInvalidSessionId;
    mutable std::mutex stateMu_;
    WelcomeConfig cfg_;
    std::atomic<bool> autoReconnectDesired_{false};
    std::atomic<bool> connectionLostPosted_{false};
    std::atomic<bool> notifyUiCallbacks_{true};

    StatusCallback onStatus_;
    ConnectedCallback onConnected_;
    WelcomeCallback onWelcome_;
    OpusFrameCallback onOpusFrame_;
    TxCountdownStartCallback onTxCountdownStart_;
    ServerPttLockCallback onServerPttLock_;
    ServerPttUnlockCallback onServerPttUnlock_;
    RxBroadcastStartCallback onRxBroadcastStart_;
    RxBroadcastEndCallback onRxBroadcastEnd_;
    TxStopCallback onTxStop_;
    ConnectionLostCallback onConnectionLost_;
};
