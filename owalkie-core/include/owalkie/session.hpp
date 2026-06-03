#pragma once

#include <functional>
#include <memory>
#include <span>

#include "owalkie/types.hpp"

namespace owalkie {

struct SessionCallbacks {
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

    bool isConnected() const;
    void setAutoReconnect(bool enabled);
    bool autoReconnectEnabled() const;

    Result feedTxPcm(std::span<const int16_t> samples);
    Result sendTxEofBurst();
    Result setRepeaterMode(bool enabled);
    Result resetUdpTransport();
    void setPowerProfile(PowerProfile profile);
    PowerProfile powerProfile() const;
    void enterUdpRecovery();
    void notifyNetworkChanged();
    Result punchUdpNat();
    Result setTxSignalStrength(int strength);
    int txSignalStrength() const;

    SessionState state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owalkie
