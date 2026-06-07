#pragma once

#include <functional>
#include <memory>
#include <span>

#include "owalkie/types.hpp"

namespace owalkie {

struct SessionCallbacks {
    /// If set, inbound Opus payloads are delivered without decoding (desktop client path).
    std::function<void(std::span<const uint8_t> opus)> onRxOpus;
    std::function<void(std::span<const int16_t> pcm, int sampleRate, int packetMs)> onRxPcm;
    std::function<void(const Event& event)> onSessionEvent;
};

class Session {
public:
    static Result create(std::unique_ptr<Session>& out);

    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void setCallbacks(SessionCallbacks callbacks);
    Result connect(const ConnectParams& params);
    void disconnect();
    /// Stop I/O without blocking the caller (safe during WS read/callback thread).
    void haltTransport();
    /// Block until teardown completes. Negative @p timeoutMs means no time limit.
    void waitUntilTransportStopped(int timeoutMs = -1);

    bool isConnected() const;
    bool isSessionReady() const;
    bool isConnectionLost() const;

    Result reconnectTeardown();
    Result reconnectConnect(const ConnectParams& params);
    Result reconnectConnect(const ConnectParams& params, int timeoutMs);
    void emitConnectionLost(const std::string& reason);
    void emitConnectionFailed(const std::string& reason);
    void clearConnectionLostSignaled();
    void cancelOngoingConnect();

    Result txStart();
    Result pushTxPcm(std::span<const int16_t> samples);
    Result txEnd();

    Result sendTxOpus(std::span<const uint8_t> opus);
    Result sendTxEofBurst();
    Result setRepeaterMode(bool enabled);
    Result resetUdpTransport();
    void setPowerProfile(PowerProfile profile);
    PowerProfile powerProfile() const;
    Result punchUdpNat();

    SessionState state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owalkie
