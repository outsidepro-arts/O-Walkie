#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace owalkie {

constexpr int kProtocolVersion = 2;
constexpr int kRogerDefaultTailMs = 40;

enum class Result {
    Ok = 0,
    InvalidArg,
    AlreadyConnected,
    NotConnected,
    NotReady,
    Protocol,
    Network,
    Internal,
    Unsupported,
    BufferTooSmall,
    /** Valid server message that does not produce a session event (e.g. joined, pong). */
    NoEvent,
};

enum class EventType {
    Connecting = 0,
    TransportConnected = 1,
    Disconnected = 2,
    ProtocolError = 3,
    Welcome = 4,
    RxBroadcastStart = 5,
    RxBroadcastEnd = 6,
    LocalTxStart = 7,
    LocalTxEnd = 8,
    PttLocked = 9,
    PttUnlocked = 10,
    TxCountdownStart = 11,
    TxStop = 12,
    UdpTransportReady = 13,
    UdpTransportLost = 14,
    ConnectionFailed = 15,
    Connected = 16,
    ConnectionLost = 17,
};

struct OpusConfig {
    int bitrate = 12000;
    int complexity = 5;
    bool fec = true;
    bool dtx = false;
    std::string application = "voip";
};

struct WelcomeConfig {
    uint32_t sessionId = 0;
    int protocolVersion = kProtocolVersion;
    int sampleRate = 8000;
    int packetMs = 20;
    OpusConfig opus{};
    bool busyMode = false;
    int transmitTimeoutSec = 60;
};

struct Event {
    EventType type = EventType::Connecting;
    WelcomeConfig welcome{};
    bool rxBusyMode = false;
    int rxEndDelayMs = 0;
    int pttDisplaySec = 0;
    std::string txStopInfo;
    std::string protocolError;
    int disconnectCode = 0;
    std::string disconnectReason;
};

struct ConnectParams {
    std::string host;
    int port = 0;
    std::string channel;
    bool useTls = false;
    bool repeaterMode = false;
};

enum class PowerProfile {
    Foreground,
    Background,
    ActiveTx,
};

struct SessionState {
    bool connected = false;
    bool udpReady = false;
    bool connectionLost = false;
    bool receiving = false;
    bool localTxActive = false;
    bool pttServerLocked = false;
    int pttLockDisplaySec = 0;
    WelcomeConfig welcome{};
    bool hasWelcome = false;
};

struct SignalPoint {
    double freqHz = 0.0;
    int durationMs = 0;
};

struct SignalPattern {
    std::vector<SignalPoint> points;
    int tailMs = 0;
    int repeatCount = 1;
    double gain = 0.26;
};

struct UdpAudioPacket {
    uint32_t sessionId = 0;
    uint32_t sequence = 0;
    uint8_t signalStrength = 0;
    std::vector<uint8_t> opus;
};

} // namespace owalkie
