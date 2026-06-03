#include "owalkie/session.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/error_code.hpp>

#include "opus_codec.hpp"
#include "owalkie/json.hpp"
#include "owalkie/protocol.hpp"
#include "owalkie/udp.hpp"

namespace owalkie {
namespace {

namespace ws = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;
using AsioUdp = ::boost::asio::ip::udp;

constexpr int64_t kNsPerSec = 1'000'000'000LL;
constexpr int64_t kNsPerMs = 1'000'000LL;
constexpr int64_t kUdpKeepaliveRecoveryWindowNs = 90LL * kNsPerSec;
constexpr int64_t kNetworkRecoveryMinIntervalNs = 3LL * kNsPerSec;
constexpr int kKeepaliveJitterPercent = 15;

struct KeepaliveTimings {
    int64_t idleIntervalNs;
    int64_t recoveryIntervalNs;
    int64_t rtxNs;
    int64_t lostNs;
    int pendingPollMs;
};

KeepaliveTimings keepaliveTimingsFor(PowerProfile profile) {
    switch (profile) {
        case PowerProfile::Background:
            return {
                50LL * kNsPerSec,
                12LL * kNsPerSec,
                2000LL * kNsPerMs,
                10000LL * kNsPerMs,
                700,
            };
        case PowerProfile::Foreground:
        case PowerProfile::ActiveTx:
        default:
            return {
                12LL * kNsPerSec,
                6LL * kNsPerSec,
                1000LL * kNsPerMs,
                8000LL * kNsPerMs,
                300,
            };
    }
}

int64_t jitteredKeepaliveDelayMs(int64_t intervalSec, std::mt19937& rng) {
    const int64_t baseMs = intervalSec * 1000LL;
    const int64_t deltaMs = (baseMs * kKeepaliveJitterPercent) / 100LL;
    const int64_t minMs = std::max<int64_t>(1000LL, baseMs - deltaMs);
    const int64_t maxMs = baseMs + deltaMs;
    if (maxMs <= minMs) {
        return minMs;
    }
    std::uniform_int_distribution<int64_t> dist(minMs, maxMs);
    return dist(rng);
}

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct ParsedHost {
    std::string host;
    int port = 0;
    bool useTls = false;
};

ParsedHost parseHostEndpoint(const std::string& rawHost, int fallbackPort) {
    ParsedHost out{};
    std::string value = rawHost;
    auto lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
    if (lowered.rfind("wss://", 0) == 0) {
        out.useTls = true;
        value = value.substr(6);
    } else if (lowered.rfind("ws://", 0) == 0) {
        value = value.substr(5);
    } else if (lowered.rfind("https://", 0) == 0) {
        out.useTls = true;
        value = value.substr(8);
    } else if (lowered.rfind("http://", 0) == 0) {
        value = value.substr(7);
    }

    const auto slash = value.find('/');
    if (slash != std::string::npos) {
        value = value.substr(0, slash);
    }

    out.port = fallbackPort;
    out.host = value;
    if (value.empty()) {
        return out;
    }

    if (value.front() == '[') {
        const auto closing = value.find(']');
        if (closing != std::string::npos) {
            out.host = value.substr(1, closing - 1);
            const auto rest = value.substr(closing + 1);
            if (!rest.empty() && rest.front() == ':') {
                out.port = std::stoi(rest.substr(1));
            }
        }
        return out;
    }

    const auto colonCount = std::count(value.begin(), value.end(), ':');
    if (colonCount == 1) {
        const auto idx = value.rfind(':');
        try {
            out.port = std::stoi(value.substr(idx + 1));
            out.host = value.substr(0, idx);
        } catch (...) {
            out.host = value;
        }
    }
    return out;
}

} // namespace

struct Session::Impl {
    std::mutex cbMu;
    SessionCallbacks callbacks{};

    mutable std::mutex stateMu;
    SessionState sessionState{};

    boost::asio::io_context ioc;
    tcp::resolver resolver{ioc};
    std::optional<ws::stream<tcp::socket>> wsStream;
    std::optional<AsioUdp::socket> udpSocket;
    AsioUdp::endpoint udpRemote;
    std::mutex udpMu;
    std::atomic<bool> udpResetRequested{false};

    std::atomic<PowerProfile> powerProfile{PowerProfile::Foreground};
    std::atomic<int64_t> udpRecoveryUntilNs{0};
    std::atomic<bool> pendingNetworkRecreate{false};
    std::atomic<int64_t> lastNetworkRecoveryAtNs{0};
    std::atomic<uint8_t> txSignalStrength{pkt::kDefaultTxSignalStrength};

    std::atomic<bool> connected{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> autoReconnect{true};
    std::atomic<int> seq{0};

    WelcomeConfig welcome{};
    ConnectParams connectParams{};
    std::string hostForWs;
    bool repeaterMode = false;

    codec::OpusCodec opus{};
    std::vector<int16_t> txPcmBuffer;

    std::thread wsThread;
    std::thread udpThread;
    std::thread keepaliveThread;

    std::atomic<int64_t> lastInboundNs{0};
    std::atomic<int64_t> lastUdpKeepaliveSentNs{0};
    std::atomic<int64_t> lastUdpKeepalivePendingSinceNs{0};

    void emitEvent(const Event& event) {
        std::lock_guard<std::mutex> lg(cbMu);
        if (callbacks.onSessionEvent) {
            callbacks.onSessionEvent(event);
        }
        std::lock_guard<std::mutex> sl(stateMu);
        switch (event.type) {
            case EventType::Connected:
                sessionState.connected = true;
                break;
            case EventType::Disconnected:
            case EventType::ProtocolError:
                sessionState.connected = false;
                sessionState.udpReady = false;
                break;
            case EventType::Welcome:
                sessionState.welcome = event.welcome;
                sessionState.hasWelcome = true;
                break;
            case EventType::RxBroadcastStart:
                sessionState.receiving = true;
                break;
            case EventType::RxBroadcastEnd:
                sessionState.receiving = false;
                break;
            case EventType::LocalTxStart:
                sessionState.localTxActive = true;
                break;
            case EventType::LocalTxEnd:
                sessionState.localTxActive = false;
                break;
            case EventType::PttLocked:
                sessionState.pttServerLocked = true;
                sessionState.pttLockDisplaySec = event.pttDisplaySec;
                break;
            case EventType::PttUnlocked:
                sessionState.pttServerLocked = false;
                sessionState.pttLockDisplaySec = 0;
                break;
            case EventType::UdpTransportReady:
                sessionState.udpReady = true;
                break;
            case EventType::UdpTransportLost:
                sessionState.udpReady = false;
                break;
            default:
                break;
        }
    }

    void emitRxPcm(std::span<const int16_t> pcm) {
        std::lock_guard<std::mutex> lg(cbMu);
        if (callbacks.onRxPcm) {
            callbacks.onRxPcm(pcm, welcome.sampleRate, welcome.packetMs);
        }
    }

    void closeSockets() {
        udpResetRequested.store(true);
        boost::system::error_code ec;
        try {
            std::lock_guard<std::mutex> lg(udpMu);
            if (udpSocket) {
                udpSocket->cancel(ec);
            }
        } catch (...) {
        }
        ec.clear();
        try {
            if (wsStream) {
                wsStream->next_layer().cancel(ec);
            }
        } catch (...) {
        }
        ec.clear();
        try {
            if (wsStream && wsStream->is_open()) {
                wsStream->close(ws::close_code::going_away, ec);
            }
        } catch (...) {
        }
        ec.clear();
        try {
            std::lock_guard<std::mutex> lg(udpMu);
            if (udpSocket) {
                udpSocket->close(ec);
            }
        } catch (...) {
        }
    }

    void joinThreads() {
        const auto tid = std::this_thread::get_id();
        if (wsThread.joinable() && wsThread.get_id() != tid) {
            wsThread.join();
        }
        if (udpThread.joinable() && udpThread.get_id() != tid) {
            udpThread.join();
        }
        if (keepaliveThread.joinable() && keepaliveThread.get_id() != tid) {
            keepaliveThread.join();
        }
        wsStream.reset();
        {
            std::lock_guard<std::mutex> lg(udpMu);
            udpSocket.reset();
        }
        connected.store(false);
        lastUdpKeepaliveSentNs.store(0);
        lastUdpKeepalivePendingSinceNs.store(0);
        udpRecoveryUntilNs.store(0);
        pendingNetworkRecreate.store(false);
        lastNetworkRecoveryAtNs.store(0);
        udpResetRequested.store(false);
        txSignalStrength.store(pkt::kDefaultTxSignalStrength);
    }

    void sendWsText(const std::string& text) {
        if (!wsStream || !connected.load()) {
            return;
        }
        boost::system::error_code ec;
        std::lock_guard<std::mutex> lg(stateMu);
        if (!wsStream || !wsStream->is_open()) {
            return;
        }
        wsStream->write(boost::asio::buffer(text), ec);
    }

    void sendUdpPayload(const uint8_t* data, size_t size) {
        if (!udpSocket || !connected.load() || welcome.sessionId == 0) {
            return;
        }
        try {
            std::lock_guard<std::mutex> lg(udpMu);
            if (!udpSocket) {
                return;
            }
            udpSocket->send_to(boost::asio::buffer(data, size), udpRemote);
        } catch (...) {
        }
    }

    void sendOpusFrame(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        UdpAudioPacket pkt{};
        pkt.sessionId = welcome.sessionId;
        pkt.sequence = static_cast<uint32_t>(seq.fetch_add(1) + 1);
        pkt.signalStrength = txSignalStrength.load();
        pkt.opus.assign(data, data + size);
        std::vector<uint8_t> payload;
        if (owalkie::pkt::pack(pkt, payload) != Result::Ok) {
            return;
        }
        sendUdpPayload(payload.data(), payload.size());
    }

    void sendUdpKeepalive() {
        if (!udpSocket || welcome.sessionId == 0) {
            return;
        }
        std::array<uint8_t, 9> payload{};
        payload[0] = static_cast<uint8_t>((welcome.sessionId >> 24) & 0xFF);
        payload[1] = static_cast<uint8_t>((welcome.sessionId >> 16) & 0xFF);
        payload[2] = static_cast<uint8_t>((welcome.sessionId >> 8) & 0xFF);
        payload[3] = static_cast<uint8_t>(welcome.sessionId & 0xFF);
        payload[8] = owalkie::pkt::kKeepaliveSignal;
        sendUdpPayload(payload.data(), payload.size());
        const int64_t now = nowNs();
        lastUdpKeepaliveSentNs.store(now);
        int64_t expected = 0;
        (void)lastUdpKeepalivePendingSinceNs.compare_exchange_strong(expected, now);
    }

    void sendUdpTxEof() {
        if (!udpSocket || welcome.sessionId == 0) {
            return;
        }
        std::array<uint8_t, 9> payload{};
        payload[0] = static_cast<uint8_t>((welcome.sessionId >> 24) & 0xFF);
        payload[1] = static_cast<uint8_t>((welcome.sessionId >> 16) & 0xFF);
        payload[2] = static_cast<uint8_t>((welcome.sessionId >> 8) & 0xFF);
        payload[3] = static_cast<uint8_t>(welcome.sessionId & 0xFF);
        const uint32_t seqNum = static_cast<uint32_t>(seq.fetch_add(1) + 1);
        payload[4] = static_cast<uint8_t>((seqNum >> 24) & 0xFF);
        payload[5] = static_cast<uint8_t>((seqNum >> 16) & 0xFF);
        payload[6] = static_cast<uint8_t>((seqNum >> 8) & 0xFF);
        payload[7] = static_cast<uint8_t>(seqNum & 0xFF);
        payload[8] = 0;
        sendUdpPayload(payload.data(), payload.size());
    }

    void beginLocalTxIfNeeded() {
        bool expected = false;
        {
            std::lock_guard<std::mutex> lg(stateMu);
            expected = !sessionState.localTxActive;
        }
        if (expected) {
            Event ev{};
            ev.type = EventType::LocalTxStart;
            emitEvent(ev);
        }
    }

    void enterUdpRecoveryInternal() {
        udpRecoveryUntilNs.store(nowNs() + kUdpKeepaliveRecoveryWindowNs);
    }

    void drainPendingNetworkRecreateIfIdle() {
        if (!pendingNetworkRecreate.exchange(false)) {
            return;
        }
        if (!connected.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lg(stateMu);
            if (sessionState.localTxActive) {
                pendingNetworkRecreate.store(true);
                return;
            }
        }
        const int64_t now = nowNs();
        const int64_t last = lastNetworkRecoveryAtNs.load();
        if (last != 0 && now - last < kNetworkRecoveryMinIntervalNs) {
            pendingNetworkRecreate.store(true);
            return;
        }
        lastNetworkRecoveryAtNs.store(now);
        (void)resetUdpInternal();
        enterUdpRecoveryInternal();
    }

    void endLocalTxIfNeeded() {
        bool expected = true;
        {
            std::lock_guard<std::mutex> lg(stateMu);
            expected = sessionState.localTxActive;
        }
        if (expected) {
            Event ev{};
            ev.type = EventType::LocalTxEnd;
            emitEvent(ev);
            drainPendingNetworkRecreateIfIdle();
        }
    }

    void handleWelcome(const WelcomeConfig& cfg) {
        welcome = cfg;
        opus.configure(welcome);
        txPcmBuffer.clear();

        Event ev{};
        ev.type = EventType::Welcome;
        ev.welcome = cfg;
        emitEvent(ev);

        sendWsText(json::buildJoin(connectParams.channel));
        const int localPort = udpSocket ? static_cast<int>(udpSocket->local_endpoint().port()) : 0;
        sendWsText(json::buildUdpHello(localPort));
        sendWsText(json::buildRepeaterMode(repeaterMode));
        enterUdpRecoveryInternal();
        sendUdpKeepalive();
    }

    void handleWsText(const std::string& text) {
        Event event{};
        const Result parsed = json::parseServerMessage(text, event);
        if (parsed != Result::Ok) {
            return;
        }
        if (event.type == EventType::Welcome) {
            handleWelcome(event.welcome);
            return;
        }
        emitEvent(event);
    }

    void wsReadLoop() {
        boost::system::error_code ec;
        while (connected.load() && !stopRequested.load()) {
            if (!wsStream || !wsStream->is_open()) {
                break;
            }
            boost::beast::flat_buffer buffer;
            wsStream->read(buffer, ec);
            if (ec) {
                connected.store(false);
                Event ev{};
                ev.type = EventType::Disconnected;
                ev.disconnectReason = ec.message();
                emitEvent(ev);
                break;
            }
            handleWsText(boost::beast::buffers_to_string(buffer.data()));
        }
    }

    void udpReadLoop() {
        boost::system::error_code ec;
        std::array<uint8_t, 1500> data{};
        while (connected.load() && !stopRequested.load()) {
            if (!udpSocket) {
                break;
            }
            AsioUdp::endpoint ep;
            const std::size_t n = udpSocket->receive_from(boost::asio::buffer(data), ep, 0, ec);
            if (ec) {
                if (stopRequested.load() || udpResetRequested.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                connected.store(false);
                Event ev{};
                ev.type = EventType::UdpTransportLost;
                emitEvent(ev);
                break;
            }
            const auto buf = std::span<const uint8_t>(data.data(), n);
            if (owalkie::pkt::isKeepaliveAck(buf, welcome.sessionId)) {
                lastInboundNs.store(nowNs());
                lastUdpKeepalivePendingSinceNs.store(0);
                continue;
            }
            if (n <= 9) {
                continue;
            }
            lastInboundNs.store(nowNs());

            owalkie::UdpAudioPacket pkt{};
            if (owalkie::pkt::unpack(buf, pkt) != Result::Ok || pkt.opus.empty()) {
                continue;
            }

            std::vector<int16_t> pcm;
            if (opus.decode(pkt.opus, opus.frameSamples(), pcm) != Result::Ok) {
                continue;
            }
            emitRxPcm(pcm);
        }
    }

    void keepaliveLoop() {
        std::mt19937 rng(std::random_device{}());
        while (connected.load() && !stopRequested.load()) {
            if (welcome.sessionId == 0) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            const int64_t now = nowNs();
            const PowerProfile profile = powerProfile.load();
            const KeepaliveTimings timings = keepaliveTimingsFor(profile);
            const bool inRecovery = now < udpRecoveryUntilNs.load();
            const int64_t intervalSec = inRecovery
                ? (timings.recoveryIntervalNs / kNsPerSec)
                : (timings.idleIntervalNs / kNsPerSec);
            const int64_t intervalNs = intervalSec * kNsPerSec;

            bool localTxActive = false;
            {
                std::lock_guard<std::mutex> lg(stateMu);
                localTxActive = sessionState.localTxActive;
            }
            const bool skipIdleKeepalive = localTxActive || profile == PowerProfile::ActiveTx;

            const int64_t pendingSince = lastUdpKeepalivePendingSinceNs.load();
            if (pendingSince != 0) {
                if (now - pendingSince >= timings.lostNs) {
                    lastUdpKeepalivePendingSinceNs.store(0);
                    (void)resetUdpInternal();
                    enterUdpRecoveryInternal();
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
                const int64_t lastSent = lastUdpKeepaliveSentNs.load();
                if (lastSent == 0 || now - lastSent >= timings.rtxNs) {
                    sendUdpKeepalive();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(timings.pendingPollMs));
                continue;
            }

            if (!skipIdleKeepalive) {
                const int64_t lastSent = lastUdpKeepaliveSentNs.load();
                if (lastSent == 0 || now - lastSent >= intervalNs) {
                    sendUdpKeepalive();
                }
            }

            const int64_t delayMs = jitteredKeepaliveDelayMs(intervalSec, rng);
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }

    void setPowerProfileInternal(PowerProfile profile) {
        powerProfile.store(profile);
    }

    PowerProfile powerProfileInternal() const {
        return powerProfile.load();
    }

    void enterUdpRecoveryInternalPublic() {
        enterUdpRecoveryInternal();
    }

    void notifyNetworkChangedInternal() {
        if (!connected.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lg(stateMu);
            if (sessionState.localTxActive) {
                pendingNetworkRecreate.store(true);
                return;
            }
        }
        const int64_t now = nowNs();
        const int64_t last = lastNetworkRecoveryAtNs.load();
        if (last != 0 && now - last < kNetworkRecoveryMinIntervalNs) {
            return;
        }
        lastNetworkRecoveryAtNs.store(now);
        (void)resetUdpInternal();
        enterUdpRecoveryInternal();
    }

    Result punchUdpNatInternal() {
        if (!connected.load() || welcome.sessionId == 0) {
            return Result::NotConnected;
        }
        sendUdpKeepalive();
        return Result::Ok;
    }

    Result setTxSignalStrengthInternal(int strength) {
        if (strength < 0 || strength > 255) {
            return Result::InvalidArg;
        }
        if (strength == static_cast<int>(pkt::kKeepaliveAckSignal)) {
            return Result::InvalidArg;
        }
        txSignalStrength.store(static_cast<uint8_t>(strength));
        return Result::Ok;
    }

    int txSignalStrengthInternal() const {
        return static_cast<int>(txSignalStrength.load());
    }

    Result connectInternal(const ConnectParams& params) {
        if (connected.load()) {
            return Result::AlreadyConnected;
        }

        closeSockets();
        joinThreads();

        connectParams = params;
        const ParsedHost parsed = parseHostEndpoint(params.host, params.port);
        if (parsed.host.empty() || params.port < 1 || params.port > 65535) {
            return Result::InvalidArg;
        }
        if (params.useTls || parsed.useTls) {
            return Result::Unsupported;
        }

        repeaterMode = params.repeaterMode;
        hostForWs = parsed.host;
        stopRequested.store(false);
        seq.store(0);
        welcome = WelcomeConfig{};
        txPcmBuffer.clear();
        opus.reset();

        Event connecting{};
        connecting.type = EventType::Connecting;
        emitEvent(connecting);

        try {
            auto results = resolver.resolve(hostForWs, std::to_string(params.port));
            wsStream.emplace(ioc);
            boost::asio::connect(wsStream->next_layer(), results.begin(), results.end());
            wsStream->handshake(hostForWs, "/ws");

            const tcp::endpoint tcpRemote = wsStream->next_layer().remote_endpoint();
            const auto peerAddr = tcpRemote.address();
            if (peerAddr.is_v4()) {
                udpSocket.emplace(ioc, AsioUdp::endpoint(AsioUdp::v4(), 0));
            } else {
                udpSocket.emplace(ioc, AsioUdp::endpoint(AsioUdp::v6(), 0));
            }
            udpRemote = AsioUdp::endpoint(peerAddr, static_cast<std::uint16_t>(params.port));

            connected.store(true);
            Event connectedEv{};
            connectedEv.type = EventType::Connected;
            emitEvent(connectedEv);
            Event udpReady{};
            udpReady.type = EventType::UdpTransportReady;
            emitEvent(udpReady);
            enterUdpRecoveryInternal();

            wsThread = std::thread([this] { wsReadLoop(); });
            udpThread = std::thread([this] { udpReadLoop(); });
            keepaliveThread = std::thread([this] { keepaliveLoop(); });
            return Result::Ok;
        } catch (const std::exception& ex) {
            Event err{};
            err.type = EventType::Disconnected;
            err.disconnectReason = ex.what();
            emitEvent(err);
            closeSockets();
            joinThreads();
            return Result::Network;
        }
    }

    void disconnectInternal() {
        stopRequested.store(true);
        connected.store(false);
        closeSockets();
        joinThreads();
        txPcmBuffer.clear();
        endLocalTxIfNeeded();
        Event ev{};
        ev.type = EventType::Disconnected;
        emitEvent(ev);
    }

    Result feedTxPcmInternal(std::span<const int16_t> samples) {
        if (!connected.load() || welcome.sessionId == 0) {
            return Result::NotConnected;
        }
        beginLocalTxIfNeeded();
        txPcmBuffer.insert(txPcmBuffer.end(), samples.begin(), samples.end());
        const int frame = opus.frameSamples();
        if (frame <= 0) {
            return Result::Internal;
        }
        std::vector<uint8_t> encoded;
        while (static_cast<int>(txPcmBuffer.size()) >= frame) {
            const auto frameSpan = std::span<const int16_t>(txPcmBuffer.data(), static_cast<size_t>(frame));
            if (opus.encode(frameSpan, encoded) != Result::Ok) {
                return Result::Internal;
            }
            sendOpusFrame(encoded.data(), encoded.size());
            txPcmBuffer.erase(txPcmBuffer.begin(), txPcmBuffer.begin() + frame);
        }
        return Result::Ok;
    }

    Result sendTxEofBurstInternal() {
        if (!connected.load()) {
            return Result::NotConnected;
        }
        txPcmBuffer.clear();
        static constexpr int delays[] = {0, 20, 60};
        for (int d : delays) {
            if (d > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(d));
            }
            sendUdpTxEof();
        }
        endLocalTxIfNeeded();
        return Result::Ok;
    }

    Result resetUdpInternal() {
        if (!connected.load() || !wsStream) {
            return Result::NotConnected;
        }
        udpResetRequested.store(true);
        boost::system::error_code ec;
        {
            std::lock_guard<std::mutex> lg(udpMu);
            if (udpSocket) {
                udpSocket->close(ec);
                udpSocket.reset();
            }
        }
        try {
            const tcp::endpoint tcpRemote = wsStream->next_layer().remote_endpoint();
            const auto peerAddr = tcpRemote.address();
            int localPort = 0;
            {
                std::lock_guard<std::mutex> lg(udpMu);
                if (peerAddr.is_v4()) {
                    udpSocket.emplace(ioc, AsioUdp::endpoint(AsioUdp::v4(), 0));
                } else {
                    udpSocket.emplace(ioc, AsioUdp::endpoint(AsioUdp::v6(), 0));
                }
                udpRemote = AsioUdp::endpoint(peerAddr, static_cast<std::uint16_t>(connectParams.port));
                localPort = static_cast<int>(udpSocket->local_endpoint().port());
            }
            sendWsText(json::buildUdpHello(localPort));
            sendUdpKeepalive();
            Event ev{};
            ev.type = EventType::UdpTransportReady;
            emitEvent(ev);
            udpResetRequested.store(false);
            return Result::Ok;
        } catch (...) {
            udpResetRequested.store(false);
            Event ev{};
            ev.type = EventType::UdpTransportLost;
            emitEvent(ev);
            return Result::Network;
        }
    }
};

Result Session::create(std::unique_ptr<Session>& out) {
    out = std::make_unique<Session>();
    return Result::Ok;
}

Session::Session() : impl_(std::make_unique<Impl>()) {}

Session::~Session() {
    impl_->disconnectInternal();
}

void Session::setCallbacks(SessionCallbacks callbacks) {
    std::lock_guard<std::mutex> lg(impl_->cbMu);
    impl_->callbacks = std::move(callbacks);
}

Result Session::connect(const ConnectParams& params) {
    return impl_->connectInternal(params);
}

void Session::disconnect() {
    impl_->disconnectInternal();
}

bool Session::isConnected() const {
    return impl_->connected.load();
}

void Session::setAutoReconnect(bool enabled) {
    impl_->autoReconnect.store(enabled);
}

bool Session::autoReconnectEnabled() const {
    return impl_->autoReconnect.load();
}

Result Session::feedTxPcm(std::span<const int16_t> samples) {
    return impl_->feedTxPcmInternal(samples);
}

Result Session::sendTxEofBurst() {
    return impl_->sendTxEofBurstInternal();
}

Result Session::setRepeaterMode(bool enabled) {
    impl_->repeaterMode = enabled;
    if (!impl_->connected.load()) {
        return Result::NotConnected;
    }
    impl_->sendWsText(json::buildRepeaterMode(enabled));
    return Result::Ok;
}

Result Session::resetUdpTransport() {
    return impl_->resetUdpInternal();
}

void Session::setPowerProfile(PowerProfile profile) {
    impl_->setPowerProfileInternal(profile);
}

PowerProfile Session::powerProfile() const {
    return impl_->powerProfileInternal();
}

void Session::enterUdpRecovery() {
    impl_->enterUdpRecoveryInternalPublic();
}

void Session::notifyNetworkChanged() {
    impl_->notifyNetworkChangedInternal();
}

Result Session::punchUdpNat() {
    return impl_->punchUdpNatInternal();
}

Result Session::setTxSignalStrength(int strength) {
    return impl_->setTxSignalStrengthInternal(strength);
}

int Session::txSignalStrength() const {
    return impl_->txSignalStrengthInternal();
}

SessionState Session::state() const {
    std::lock_guard<std::mutex> lg(impl_->stateMu);
    return impl_->sessionState;
}

} // namespace owalkie
