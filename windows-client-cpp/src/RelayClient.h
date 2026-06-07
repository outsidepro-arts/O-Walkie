#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
    using PcmFrameCallback = std::function<void(const int16_t* samples, size_t count)>;
    using TxCountdownStartCallback = std::function<void()>;
    using ServerPttLockCallback = std::function<void(int displaySec)>;
    using ServerPttUnlockCallback = std::function<void()>;
    using RxBroadcastStartCallback = std::function<void(bool busyMode)>;
    using RxBroadcastEndCallback = std::function<void()>;
    using TxStopCallback = std::function<void()>;

    RelayClient();
    ~RelayClient();

    bool Connect(const std::string& host, int serverPort, const std::string& channel, bool repeater);
    void Disconnect(bool notifyCallbacks = true);
    void JoinWorkerThreads();
    bool TryReconnect(int timeoutMs = 0);
    void StartReconnectLoop();
    void StopReconnectLoop();

    bool IsConnected() const;
    WelcomeConfig CurrentConfig() const;

    bool TxStart();
    void PushTxPcm(const int16_t* samples, size_t count);
    void TxEnd();
    void SetRepeaterMode(bool enabled);

    void SetStatusCallback(StatusCallback cb) { onStatus_ = std::move(cb); }
    void SetConnectedCallback(ConnectedCallback cb) { onConnected_ = std::move(cb); }
    void SetWelcomeCallback(WelcomeCallback cb) { onWelcome_ = std::move(cb); }
    void SetPcmFrameCallback(PcmFrameCallback cb) { onPcmFrame_ = std::move(cb); }
    void SetTxCountdownStartCallback(TxCountdownStartCallback cb) { onTxCountdownStart_ = std::move(cb); }
    void SetServerPttLockCallback(ServerPttLockCallback cb) { onServerPttLock_ = std::move(cb); }
    void SetServerPttUnlockCallback(ServerPttUnlockCallback cb) { onServerPttUnlock_ = std::move(cb); }
    void SetRxBroadcastStartCallback(RxBroadcastStartCallback cb) { onRxBroadcastStart_ = std::move(cb); }
    void SetRxBroadcastEndCallback(RxBroadcastEndCallback cb) { onRxBroadcastEnd_ = std::move(cb); }
    void SetTxStopCallback(TxStopCallback cb) { onTxStop_ = std::move(cb); }
    void DetachUiCallbacks();

private:
    static WelcomeConfig MapWelcome(const owalkie::WelcomeConfig& src);
    void HandleSessionEvent(const owalkie::Event& event);
    void clearManagedSession();

    owalkie::SessionId sessionId_ = owalkie::kInvalidSessionId;
    mutable std::mutex stateMu_;
    WelcomeConfig cfg_;
    std::atomic<bool> notifyUiCallbacks_{true};
    std::thread reconnectThread_;
    std::atomic<bool> reconnectLoopRunning_{false};
    std::atomic<bool> connectInFlight_{false};
    int reconnectBackoffMs_ = 1500;

    StatusCallback onStatus_;
    ConnectedCallback onConnected_;
    WelcomeCallback onWelcome_;
    PcmFrameCallback onPcmFrame_;
    TxCountdownStartCallback onTxCountdownStart_;
    ServerPttLockCallback onServerPttLock_;
    ServerPttUnlockCallback onServerPttUnlock_;
    RxBroadcastStartCallback onRxBroadcastStart_;
    RxBroadcastEndCallback onRxBroadcastEnd_;
    TxStopCallback onTxStop_;
};
