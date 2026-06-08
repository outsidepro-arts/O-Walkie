#include "owalkie/session.hpp"

#include "owalkie/link_signal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
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
constexpr int64_t kWsHeartbeatIntervalNs = 30LL * kNsPerSec;
constexpr int kKeepaliveJitterPercent = 15;
constexpr int kSocketRecvTimeoutMs = 2000;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kResolveTimeoutMs = 2500;
constexpr int kReconnectConnectTimeoutMs = 3500;

void setSocketRecvTimeoutMs(boost::asio::ip::tcp::socket& socket, int timeoutMs) {
    const auto handle = socket.native_handle();
#if defined(_WIN32)
    const DWORD tv = timeoutMs <= 0 ? 0 : static_cast<DWORD>(timeoutMs);
    ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv {};
    if (timeoutMs > 0) {
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
    }
    ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

void setSocketRecvTimeoutMs(AsioUdp::socket& socket, int timeoutMs) {
    if (timeoutMs <= 0) {
        return;
    }
    const auto handle = socket.native_handle();
#if defined(_WIN32)
    const DWORD tv = static_cast<DWORD>(timeoutMs);
    ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv {};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

bool isSocketRecvTimeout(const boost::system::error_code& ec) {
#if defined(_WIN32)
    return ec.value() == WSAETIMEDOUT;
#else
    return ec == boost::asio::error::timed_out;
#endif
}

bool isBenignUdpSocketError(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::interrupted || ec == boost::asio::error::operation_aborted) {
        return true;
    }
    const std::string msg = ec.message();
    return msg.find("Interrupted") != std::string::npos ||
        msg.find("interrupted") != std::string::npos ||
        msg.find("Operation canceled") != std::string::npos ||
        msg.find("operation canceled") != std::string::npos;
}

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
    std::mutex wsIoMu;
    std::mutex wsOutboundMu;
    std::deque<std::string> wsOutboundQueue;
    bool wsWriteInProgress = false;
    std::mutex stopMu;
    std::mutex teardownMu;
    std::condition_variable stopCv;
    std::atomic<bool> udpResetRequested{false};
    std::atomic<int64_t> udpResetGraceUntilNs{0};
    std::atomic<bool> wsThreadCompletesTeardown{false};
    std::atomic<bool> disconnectEventEmitted{false};
    std::atomic<bool> disconnectStarted{false};
    std::atomic<bool> transportFullyStopped{true};

    std::mutex teardownThreadMu_;
    std::thread teardownThread_;

    std::atomic<PowerProfile> powerProfile{PowerProfile::Foreground};
    std::atomic<int64_t> udpRecoveryUntilNs{0};
    std::atomic<bool> pendingNetworkRecreate{false};
    std::atomic<int64_t> lastNetworkRecoveryAtNs{0};

    std::atomic<bool> clientTxOpen{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> autoReconnect{true};
    std::atomic<int> seq{0};

    WelcomeConfig welcome{};
    ConnectParams connectParams{};
    std::string hostForWs;
    bool repeaterMode = false;

    codec::OpusCodec opus{};
    std::mutex encodeMu;
    std::mutex decodeMu;
    std::vector<int16_t> txPcmBuffer;

    struct TxQueueItem {
        TxSubmitOp op = TxSubmitOp::Abort;
        std::vector<int16_t> pcm;
        std::vector<uint8_t> opusBytes;
    };

    static constexpr size_t kTxQueueCapacity = 48;
    static constexpr int kTxQueueSubmitWaitMs = 400;
    static constexpr int64_t kTxOpenWatchdogNs = 2LL * kNsPerSec;

    std::mutex txQueueMu;
    std::condition_variable txQueueCv;
    std::deque<TxQueueItem> txQueue;
    std::thread txWorkerThread;
    std::atomic<bool> txWorkerStop{false};
    std::atomic<bool> txWorkerRunning{false};
    std::atomic<bool> txWorkerProcessing{false};
    int64_t txOpenAtNs = 0;
    int64_t txLastActivityNs = 0;

    std::thread wsThread;
    std::thread udpThread;
    std::thread keepaliveThread;

    std::atomic<int64_t> lastInboundNs{0};
    std::atomic<int64_t> lastUdpKeepaliveSentNs{0};
    std::atomic<int64_t> lastUdpKeepalivePendingSinceNs{0};
    std::atomic<int64_t> lastWsHeartbeatSentNs{0};
    std::atomic<bool> connectionLostSignaled{false};

    void applyEventState(const Event& event) {
        switch (event.type) {
            case EventType::TransportConnected:
                sessionState.connected = true;
                break;
            case EventType::Disconnected:
            case EventType::ProtocolError:
                sessionState.connected = false;
                sessionState.udpReady = false;
                sessionState.connectionLost = false;
                break;
            case EventType::Welcome:
                sessionState.welcome = event.welcome;
                sessionState.hasWelcome = true;
                break;
            case EventType::Connected:
                sessionState.welcome = event.welcome;
                sessionState.hasWelcome = true;
                sessionState.connected = true;
                sessionState.udpReady = true;
                sessionState.connectionLost = false;
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
            case EventType::ConnectionLost:
                sessionState.connected = false;
                sessionState.udpReady = false;
                sessionState.connectionLost = true;
                sessionState.receiving = false;
                break;
            default:
                break;
        }
    }

    void emitEvent(const Event& event) {
        {
            std::lock_guard<std::mutex> sl(stateMu);
            applyEventState(event);
        }
        std::function<void(const Event&)> eventCb;
        {
            std::lock_guard<std::mutex> lg(cbMu);
            eventCb = callbacks.onSessionEvent;
        }
        if (eventCb) {
            eventCb(event);
        }
    }

    void emitConnectionFailedInternal(const std::string& reason) {
        Event ev{};
        ev.type = EventType::ConnectionFailed;
        ev.disconnectReason = reason;
        emitEvent(ev);
    }

    void emitConnectionLostInternal(const std::string& reason) {
        if (disconnectStarted.load()) {
            return;
        }
        connectionLostSignaled.store(true);

        Event rxEnd{};
        rxEnd.type = EventType::RxBroadcastEnd;
        emitEvent(rxEnd);

        Event ev{};
        ev.type = EventType::ConnectionLost;
        ev.disconnectReason = reason;
        emitEvent(ev);
    }

    void signalRecoverableLoss(const std::string& reason) {
        if (disconnectStarted.load()) {
            return;
        }
        connected.store(false);
        haltTransportInternal();
        welcome = WelcomeConfig{};
        // Do not releaseWsStream here: often called from wsIoLoop while status is open.
        // Client connect()/disconnect paths join workers then clear transport.

        bool expected = false;
        if (!connectionLostSignaled.compare_exchange_strong(expected, true)) {
            return;
        }

        Event rxEnd{};
        rxEnd.type = EventType::RxBroadcastEnd;
        emitEvent(rxEnd);

        Event ev{};
        ev.type = EventType::ConnectionLost;
        ev.disconnectReason = reason;
        emitEvent(ev);
    }

    Result resolveWithTimeout(
        const std::string& host,
        const std::string& port,
        int timeoutMs,
        tcp::resolver::results_type& resultsOut) {
        bool completed = false;
        boost::system::error_code resolveEc;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        resolver.async_resolve(
            host,
            port,
            [&](const boost::system::error_code& ec, tcp::resolver::results_type results) {
                completed = true;
                resolveEc = ec;
                if (!ec) {
                    resultsOut = std::move(results);
                }
            });

        while (!completed && !stopRequested.load()) {
            ioc.run_for(std::chrono::milliseconds(200));
            if (std::chrono::steady_clock::now() >= deadline) {
                resolver.cancel();
                break;
            }
        }
        if (!completed) {
            resolver.cancel();
            (void)ioc.run_for(std::chrono::milliseconds(100));
        }
        ioc.restart();

        if (stopRequested.load()) {
            return Result::Network;
        }
        if (!completed || resolveEc) {
            return Result::Network;
        }
        return Result::Ok;
    }

    Result connectTcpWithTimeout(
        tcp::socket& socket,
        const tcp::resolver::results_type& results,
        int timeoutMs) {
        boost::system::error_code connectEc = boost::asio::error::would_block;
        boost::asio::steady_timer timer(ioc);
        bool completed = false;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        timer.expires_after(std::chrono::milliseconds(timeoutMs));
        timer.async_wait([&](const boost::system::error_code& timerEc) {
            if (!timerEc && !completed) {
                boost::system::error_code cancelEc;
                socket.cancel(cancelEc);
            }
        });

        boost::asio::async_connect(
            socket,
            results,
            [&](const boost::system::error_code& ec, const tcp::endpoint&) {
                completed = true;
                (void)timer.cancel();
                connectEc = ec;
            });

        while (!completed && !stopRequested.load()) {
            ioc.run_for(std::chrono::milliseconds(200));
            if (std::chrono::steady_clock::now() >= deadline) {
                boost::system::error_code cancelEc;
                socket.cancel(cancelEc);
                break;
            }
        }
        if (!completed) {
            boost::system::error_code cancelEc;
            socket.cancel(cancelEc);
            (void)ioc.run_for(std::chrono::milliseconds(100));
        }
        ioc.restart();

        if (stopRequested.load()) {
            return Result::Network;
        }
        if (!completed || connectEc) {
            return Result::Network;
        }
        return Result::Ok;
    }

    Result handshakeWithTimeout(ws::stream<tcp::socket>& ws, const std::string& host, int timeoutMs) {
        boost::system::error_code ec = boost::asio::error::would_block;
        bool completed = false;
        boost::asio::steady_timer timer(ioc);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        timer.expires_after(std::chrono::milliseconds(timeoutMs));
        timer.async_wait([&](const boost::system::error_code& timerEc) {
            if (!timerEc && !completed) {
                boost::system::error_code cancelEc;
                ws.next_layer().cancel(cancelEc);
            }
        });

        ws.async_handshake(host, "/ws", [&](const boost::system::error_code& handshakeEc) {
            completed = true;
            (void)timer.cancel();
            ec = handshakeEc;
        });

        while (!completed && !stopRequested.load()) {
            ioc.run_for(std::chrono::milliseconds(50));
            if (std::chrono::steady_clock::now() >= deadline) {
                boost::system::error_code cancelEc;
                ws.next_layer().cancel(cancelEc);
                break;
            }
        }
        if (!completed) {
            boost::system::error_code cancelEc;
            ws.next_layer().cancel(cancelEc);
            (void)ioc.run_for(std::chrono::milliseconds(100));
        }
        ioc.restart();

        if (stopRequested.load()) {
            return Result::Network;
        }
        if (!completed || ec) {
            return Result::Network;
        }
        return Result::Ok;
    }

    void cancelOngoingConnectInternal() {
        stopRequested.store(true);
        signalStopWorkers();
        closeSockets();
    }

    void emitRxPcm(std::span<const int16_t> pcm) {
        std::lock_guard<std::mutex> lg(cbMu);
        if (callbacks.onRxPcm) {
            callbacks.onRxPcm(pcm, welcome.sampleRate, welcome.packetMs);
        }
    }

    void signalStopWorkers() {
        stopCv.notify_all();
    }

    void interruptibleSleepMs(int64_t ms) {
        if (ms <= 0 || stopRequested.load()) {
            return;
        }
        std::unique_lock<std::mutex> lk(stopMu);
        stopCv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return stopRequested.load(); });
    }

    void shutdownWsTcp() {
        boost::system::error_code ec;
        std::lock_guard<std::mutex> lg(wsIoMu);
        if (!wsStream) {
            return;
        }
        try {
            auto& tcp = wsStream->next_layer();
            tcp.cancel(ec);
            ec.clear();
            if (tcp.is_open()) {
                tcp.shutdown(tcp::socket::shutdown_both, ec);
                ec.clear();
                tcp.close(ec);
            }
        } catch (...) {
        }
    }

    void releaseWsStream() {
        std::lock_guard<std::mutex> lg(wsIoMu);
        if (!wsStream) {
            return;
        }
        boost::system::error_code ec;
        try {
            // Safe only after wsIoLoop has exited (joinWsThreadUnlessCaller).
            if (wsStream->is_open()) {
                wsStream->close(ws::close_code::going_away, ec);
            }
            auto& tcp = wsStream->next_layer();
            if (tcp.is_open()) {
                tcp.shutdown(tcp::socket::shutdown_both, ec);
                ec.clear();
                tcp.close(ec);
            }
        } catch (...) {
        }
        wsStream.reset();
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
        // Never call wsStream->close() while async WS I/O may be pending; TCP shutdown aborts it.
        shutdownWsTcp();
        ec.clear();
        try {
            std::lock_guard<std::mutex> lg(udpMu);
            if (udpSocket) {
                udpSocket->close(ec);
            }
        } catch (...) {
        }
    }

    bool onWsThread() const {
        return wsThread.joinable() && wsThread.get_id() == std::this_thread::get_id();
    }

    void joinWsThreadUnlessCaller() {
        const auto tid = std::this_thread::get_id();
        if (wsThread.joinable() && wsThread.get_id() != tid) {
            wsThread.join();
        }
    }

    void joinUdpKeepaliveThreadsUnlessCaller() {
        const auto tid = std::this_thread::get_id();
        if (udpThread.joinable() && udpThread.get_id() != tid) {
            udpThread.join();
        }
        if (keepaliveThread.joinable() && keepaliveThread.get_id() != tid) {
            keepaliveThread.join();
        }
    }

    void detachWorkerThreadsUnlessCaller() {
        const auto tid = std::this_thread::get_id();
        if (wsThread.joinable() && wsThread.get_id() != tid) {
            wsThread.detach();
        }
        if (udpThread.joinable() && udpThread.get_id() != tid) {
            udpThread.detach();
        }
        if (keepaliveThread.joinable() && keepaliveThread.get_id() != tid) {
            keepaliveThread.detach();
        }
    }

    void resetIoContextInternal() {
        ioc.stop();
        ioc.restart();
    }

    void abortTxInternal() {
        {
            std::lock_guard<std::mutex> lg(txQueueMu);
            txQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lg(encodeMu);
            txPcmBuffer.clear();
        }
        if (clientTxOpen.exchange(false)) {
            endLocalTxIfNeeded();
        }
        txOpenAtNs = 0;
        txLastActivityNs = 0;
    }

    void stopTxPipeline() {
        {
            std::lock_guard<std::mutex> lg(txQueueMu);
            txWorkerStop.store(true);
        }
        txQueueCv.notify_all();
        if (txWorkerThread.joinable()) {
            if (txWorkerThread.get_id() != std::this_thread::get_id()) {
                txWorkerThread.join();
            } else {
                txWorkerThread.detach();
            }
        }
        txWorkerRunning.store(false);
        txWorkerStop.store(false);
        {
            std::lock_guard<std::mutex> lg(txQueueMu);
            txQueue.clear();
        }
        abortTxInternal();
    }

    void ensureTxWorkerStarted() {
        if (txWorkerRunning.load()) {
            return;
        }
        std::lock_guard<std::mutex> lg(txQueueMu);
        if (txWorkerRunning.load()) {
            return;
        }
        txWorkerStop.store(false);
        txWorkerThread = std::thread([this] { txWorkerLoop(); });
        txWorkerRunning.store(true);
    }

    void paceTxFrame(int64_t& nextPaceAtNs) {
        const int packetMs = welcome.packetMs > 0 ? welcome.packetMs : 20;
        const int64_t frameNs = static_cast<int64_t>(packetMs) * kNsPerMs;
        const int64_t now = nowNs();
        if (nextPaceAtNs <= 0) {
            nextPaceAtNs = now;
        }
        const int64_t sleepNs = nextPaceAtNs - now;
        if (sleepNs > 0) {
            interruptibleSleepMs((sleepNs + kNsPerMs - 1) / kNsPerMs);
        } else if (sleepNs < -frameNs * 2) {
            nextPaceAtNs = nowNs();
        }
        nextPaceAtNs += frameNs;
    }

    void processTxItem(const TxQueueItem& item, int64_t& nextPaceAtNs) {
        switch (item.op) {
            case TxSubmitOp::Open:
                if (clientTxOpen.load()) {
                    (void)txEndInternal();
                }
                (void)txStartInternal();
                txOpenAtNs = nowNs();
                txLastActivityNs = txOpenAtNs;
                nextPaceAtNs = 0;
                break;
            case TxSubmitOp::Pcm:
                if (!clientTxOpen.load()) {
                    break;
                }
                paceTxFrame(nextPaceAtNs);
                (void)feedTxPcmInternal(item.pcm);
                txLastActivityNs = nowNs();
                break;
            case TxSubmitOp::Opus:
                if (!clientTxOpen.load()) {
                    break;
                }
                paceTxFrame(nextPaceAtNs);
                (void)sendTxOpusInternal(item.opusBytes);
                txLastActivityNs = nowNs();
                break;
            case TxSubmitOp::VoiceEnd:
                if (clientTxOpen.load()) {
                    flushTxPcmBuffer();
                }
                txLastActivityNs = nowNs();
                break;
            case TxSubmitOp::Close:
                (void)txEndInternal();
                txOpenAtNs = 0;
                txLastActivityNs = 0;
                nextPaceAtNs = 0;
                break;
            case TxSubmitOp::Abort:
                abortTxInternal();
                nextPaceAtNs = 0;
                break;
        }
    }

    void txWorkerLoop() {
        int64_t nextPaceAtNs = 0;
        for (;;) {
            TxQueueItem item;
            bool haveItem = false;
            {
                std::unique_lock<std::mutex> lock(txQueueMu);
                txQueueCv.wait_for(lock, std::chrono::milliseconds(200), [this] {
                    return txWorkerStop.load() || !txQueue.empty();
                });
                if (txWorkerStop.load() && txQueue.empty()) {
                    break;
                }
                if (txQueue.empty()) {
                    if (clientTxOpen.load() && txOpenAtNs > 0) {
                        const int64_t now = nowNs();
                        if (now - txLastActivityNs > kTxOpenWatchdogNs) {
                            lock.unlock();
                            (void)txEndInternal();
                            txOpenAtNs = 0;
                            txLastActivityNs = 0;
                            nextPaceAtNs = 0;
                        }
                    }
                    continue;
                }
                item = std::move(txQueue.front());
                txQueue.pop_front();
                haveItem = true;
            }
            if (!haveItem) {
                continue;
            }
            txWorkerProcessing.store(true);
            processTxItem(item, nextPaceAtNs);
            txWorkerProcessing.store(false);
            txQueueCv.notify_all();
        }
    }

    bool waitTxQueueIdleInternal(int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0);
        auto quiescent = [this]() {
            return txQueue.empty() && !txWorkerProcessing.load();
        };
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lg(txQueueMu);
                if (quiescent()) {
                    return true;
                }
            }
            std::unique_lock<std::mutex> lock(txQueueMu);
            txQueueCv.wait_for(lock, std::chrono::milliseconds(5), [&] { return quiescent(); });
        }
        std::lock_guard<std::mutex> lg(txQueueMu);
        return quiescent();
    }

    Result submitTxInternal(
        TxSubmitOp op,
        std::span<const int16_t> pcm,
        std::span<const uint8_t> opus) {
        if (op != TxSubmitOp::Abort && (!connected.load() || welcome.sessionId == 0)) {
            return Result::NotConnected;
        }
        if (op == TxSubmitOp::Pcm && pcm.empty()) {
            return Result::InvalidArg;
        }
        if (op == TxSubmitOp::Opus && opus.empty()) {
            return Result::InvalidArg;
        }
        if (stopRequested.load() && op != TxSubmitOp::Abort) {
            return Result::NotConnected;
        }

        TxQueueItem item;
        item.op = op;
        if (op == TxSubmitOp::Pcm) {
            item.pcm.assign(pcm.begin(), pcm.end());
        } else if (op == TxSubmitOp::Opus) {
            item.opusBytes.assign(opus.begin(), opus.end());
        }

        {
            std::unique_lock<std::mutex> lock(txQueueMu);
            if (txWorkerStop.load() && op != TxSubmitOp::Abort) {
                return Result::NotConnected;
            }
            if (op == TxSubmitOp::Pcm) {
                const auto waitDeadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kTxQueueSubmitWaitMs);
                while (txQueue.size() >= kTxQueueCapacity &&
                       std::chrono::steady_clock::now() < waitDeadline) {
                    txQueueCv.wait_for(lock, std::chrono::milliseconds(5), [this] {
                        return txQueue.size() < kTxQueueCapacity || txWorkerStop.load();
                    });
                }
                if (txQueue.size() >= kTxQueueCapacity) {
                    return Result::QueueFull;
                }
            }
            if (op == TxSubmitOp::Open) {
                txQueue.clear();
            }
            txQueue.push_back(std::move(item));
        }
        ensureTxWorkerStarted();
        txQueueCv.notify_one();
        return Result::Ok;
    }

    void clearTransportState() {
        stopTxPipeline();
        releaseWsStream();
        {
            std::lock_guard<std::mutex> lg(udpMu);
            udpSocket.reset();
        }
        resetIoContextInternal();
        connected.store(false);
        lastUdpKeepaliveSentNs.store(0);
        lastUdpKeepalivePendingSinceNs.store(0);
        udpRecoveryUntilNs.store(0);
        pendingNetworkRecreate.store(false);
        lastNetworkRecoveryAtNs.store(0);
        udpResetRequested.store(false);
        udpResetGraceUntilNs.store(0);
        clientTxOpen.store(false);
        {
            std::lock_guard<std::mutex> lg(wsOutboundMu);
            wsOutboundQueue.clear();
        }
        wsWriteInProgress = false;
    }

    void joinThreads() {
        joinWsThreadUnlessCaller();
        joinUdpKeepaliveThreadsUnlessCaller();
        clearTransportState();
    }

    void emitDisconnectedOnce() {
        bool expected = false;
        if (!disconnectEventEmitted.compare_exchange_strong(expected, true)) {
            return;
        }
        Event ev{};
        ev.type = EventType::Disconnected;
        emitEvent(ev);
    }

    void finalizeTransportTeardown() {
        joinWsThreadUnlessCaller();
        joinUdpKeepaliveThreadsUnlessCaller();
        std::lock_guard<std::mutex> lg(teardownMu);
        clearTransportState();
        welcome = WelcomeConfig{};
        seq.store(0);
        {
            std::scoped_lock lock(encodeMu, decodeMu);
            opus.reset();
            txPcmBuffer.clear();
        }
        endLocalTxIfNeeded();
        emitDisconnectedOnce();
        transportFullyStopped.store(true);
    }

    void finishDisconnectFromWsThread() {
        finalizeTransportTeardown();
    }

    void joinTeardownThreadLocked(std::unique_lock<std::mutex>& lock) {
        if (!teardownThread_.joinable()) {
            return;
        }
        lock.unlock();
        teardownThread_.join();
        lock.lock();
    }

    void pumpWsAsyncWrites() {
        if (wsWriteInProgress || stopRequested.load() || !connected.load() || !wsStream ||
            !wsStream->is_open()) {
            return;
        }
        std::shared_ptr<std::string> text;
        {
            std::lock_guard<std::mutex> lg(wsOutboundMu);
            if (wsOutboundQueue.empty()) {
                return;
            }
            text = std::make_shared<std::string>(std::move(wsOutboundQueue.front()));
            wsOutboundQueue.pop_front();
        }
        wsWriteInProgress = true;
        wsStream->text(true);
        wsStream->async_write(
            boost::asio::buffer(*text),
            [this, text](const boost::system::error_code& ec, std::size_t /*bytes*/) {
                wsWriteInProgress = false;
                if (ec) {
                    {
                        std::lock_guard<std::mutex> lg(wsOutboundMu);
                        wsOutboundQueue.clear();
                    }
                    if (!stopRequested.load()) {
                        connected.store(false);
                        const bool intentional = ec == boost::asio::error::operation_aborted;
                        if (!intentional) {
                            signalRecoverableLoss(ec.message());
                        }
                    }
                    return;
                }
                pumpWsAsyncWrites();
            });
    }

    void sendWsText(const std::string& text) {
        if (!connected.load() || stopRequested.load() || text.empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lg(wsOutboundMu);
            wsOutboundQueue.push_back(text);
        }
        boost::asio::dispatch(ioc, [this]() { pumpWsAsyncWrites(); });
    }

    void onWsAsyncRead(
        const boost::system::error_code& ec,
        const std::shared_ptr<boost::beast::flat_buffer>& buffer) {
        if (ec) {
            if (!stopRequested.load() && ec != boost::asio::error::operation_aborted &&
                ec != boost::asio::error::interrupted) {
                connected.store(false);
                signalRecoverableLoss(ec.message());
            }
            return;
        }
        const std::string payload = boost::beast::buffers_to_string(buffer->data());
        buffer->consume(buffer->size());
        handleWsText(payload);
        if (stopRequested.load() || !connected.load()) {
            return;
        }
        startWsAsyncRead();
    }

    void startWsAsyncRead() {
        if (stopRequested.load() || !connected.load() || !wsStream || !wsStream->is_open()) {
            return;
        }
        auto buffer = std::make_shared<boost::beast::flat_buffer>();
        wsStream->async_read(
            *buffer,
            [this, buffer](const boost::system::error_code& readEc, std::size_t /*bytes*/) {
                onWsAsyncRead(readEc, buffer);
            });
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

    void sendOpusFrame(const uint8_t* data, size_t size, uint8_t signalStrength) {
        if (size == 0) {
            return;
        }
        UdpAudioPacket pkt{};
        pkt.sessionId = welcome.sessionId;
        pkt.sequence = static_cast<uint32_t>(seq.fetch_add(1) + 1);
        pkt.signalStrength = signalStrength;
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
        {
            std::scoped_lock lock(encodeMu, decodeMu);
            opus.configure(welcome);
            txPcmBuffer.clear();
        }

        Event welcomeEv{};
        welcomeEv.type = EventType::Welcome;
        welcomeEv.welcome = cfg;
        emitEvent(welcomeEv);

        sendWsText(json::buildJoin(connectParams.channel));
        const int localPort = udpSocket ? static_cast<int>(udpSocket->local_endpoint().port()) : 0;
        sendWsText(json::buildUdpHello(localPort));
        sendWsText(json::buildRepeaterMode(repeaterMode));
        enterUdpRecoveryInternal();
        sendUdpKeepalive();

        Event readyEv{};
        readyEv.type = EventType::Connected;
        readyEv.welcome = cfg;
        emitEvent(readyEv);

        Event udpReady{};
        udpReady.type = EventType::UdpTransportReady;
        emitEvent(udpReady);
    }

    void handleWsText(const std::string& text) {
        Event event{};
        const Result parsed = json::parseServerMessage(text, event);
        if (parsed == Result::NoEvent) {
            return;
        }
        if (parsed != Result::Ok) {
            if (parsed == Result::Protocol) {
                Event err{};
                err.type = EventType::ProtocolError;
                err.protocolError = "protocol mismatch";
                connected.store(false);
                emitEvent(err);
                closeSockets();
                return;
            }
            // Unknown server message types are ignored; do not tear down a live session.
            return;
        }
        if (event.type == EventType::Welcome) {
            handleWelcome(event.welcome);
            return;
        }
        emitEvent(event);
    }

    void wsIoLoop() {
        boost::asio::post(ioc, [this]() { startWsAsyncRead(); });
        while (!stopRequested.load()) {
            ioc.restart();
            try {
                ioc.run();
            } catch (...) {
                break;
            }
            if (stopRequested.load() || !connected.load()) {
                break;
            }
        }
        if (wsThreadCompletesTeardown.exchange(false)) {
            finishDisconnectFromWsThread();
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
                if (stopRequested.load() || udpResetRequested.load() ||
                    nowNs() < udpResetGraceUntilNs.load()) {
                    interruptibleSleepMs(50);
                    continue;
                }
                if (!stopRequested.load() && isSocketRecvTimeout(ec)) {
                    continue;
                }
                if (isBenignUdpSocketError(ec)) {
                    interruptibleSleepMs(50);
                    continue;
                }
                signalRecoverableLoss(std::string("udp: ") + ec.message());
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

            {
                std::function<void(std::span<const uint8_t>)> rxCb;
                {
                    std::lock_guard<std::mutex> lg(cbMu);
                    rxCb = callbacks.onRxOpus;
                }
                if (rxCb) {
                    rxCb(pkt.opus);
                    continue;
                }
            }

            std::vector<int16_t> pcm;
            {
                std::lock_guard<std::mutex> lg(decodeMu);
                if (opus.decode(pkt.opus, opus.frameSamples(), pcm) != Result::Ok) {
                    continue;
                }
            }
            emitRxPcm(pcm);
        }
    }

    void keepaliveLoop() {
        std::mt19937 rng(std::random_device{}());
        while (connected.load() && !stopRequested.load()) {
            if (welcome.sessionId == 0) {
                interruptibleSleepMs(1000);
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
                    signalRecoverableLoss("keepalive_lost");
                    break;
                }
                const int64_t lastSent = lastUdpKeepaliveSentNs.load();
                if (lastSent == 0 || now - lastSent >= timings.rtxNs) {
                    sendUdpKeepalive();
                }
                interruptibleSleepMs(timings.pendingPollMs);
                continue;
            }

            if (!skipIdleKeepalive) {
                const int64_t lastSent = lastUdpKeepaliveSentNs.load();
                if (lastSent == 0 || now - lastSent >= intervalNs) {
                    sendUdpKeepalive();
                }
            }

            const int64_t lastWsHb = lastWsHeartbeatSentNs.load();
            if (lastWsHb == 0 || now - lastWsHb >= kWsHeartbeatIntervalNs) {
                sendWsText(json::buildHeartbeat());
                lastWsHeartbeatSentNs.store(now);
            }

            const int64_t delayMs = jitteredKeepaliveDelayMs(intervalSec, rng);
            interruptibleSleepMs(delayMs);
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

    uint8_t uplinkSignalByte() const {
        return static_cast<uint8_t>(link_signal::Registry::instance().combinedByte());
    }

    Result connectInternal(
        const ConnectParams& params,
        int connectTimeoutMs = kConnectTimeoutMs,
        bool afterTeardown = false) {
        if (!afterTeardown) {
            if (connected.load() || disconnectStarted.load()) {
                disconnectInternal();
                waitUntilTransportStoppedInternal(-1);
            }
            closeSockets();
            joinThreads();
            resetIoContextInternal();
        }

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
        wsThreadCompletesTeardown.store(false);
        disconnectEventEmitted.store(false);
        disconnectStarted.store(false);
        transportFullyStopped.store(false);
        resetIoContextInternal();
        seq.store(0);
        welcome = WelcomeConfig{};
        {
            std::scoped_lock lock(encodeMu, decodeMu);
            txPcmBuffer.clear();
            opus.reset();
        }

        Event connecting{};
        connecting.type = EventType::Connecting;
        emitEvent(connecting);

        try {
            tcp::resolver::results_type results;
            if (resolveWithTimeout(hostForWs, std::to_string(params.port), kResolveTimeoutMs, results) !=
                Result::Ok) {
                throw std::runtime_error("dns resolve timed out");
            }
            if (wsStream) {
                joinWsThreadUnlessCaller();
                joinUdpKeepaliveThreadsUnlessCaller();
                releaseWsStream();
            }
            wsStream.emplace(ioc);
            if (connectTcpWithTimeout(wsStream->next_layer(), results, connectTimeoutMs) !=
                Result::Ok) {
                throw std::runtime_error("tcp connect timed out");
            }
            setSocketRecvTimeoutMs(wsStream->next_layer(), kSocketRecvTimeoutMs);
            if (handshakeWithTimeout(*wsStream, hostForWs, connectTimeoutMs) != Result::Ok) {
                throw std::runtime_error("websocket handshake timed out");
            }
            // WebSocket session I/O uses async read/write on wsIoLoop's io_context.
            setSocketRecvTimeoutMs(wsStream->next_layer(), 0);

            const tcp::endpoint tcpRemote = wsStream->next_layer().remote_endpoint();
            const auto peerAddr = tcpRemote.address();
            if (peerAddr.is_v4()) {
                udpSocket.emplace(ioc, AsioUdp::endpoint(AsioUdp::v4(), 0));
            } else {
                udpSocket.emplace(ioc, AsioUdp::endpoint(AsioUdp::v6(), 0));
            }
            setSocketRecvTimeoutMs(*udpSocket, kSocketRecvTimeoutMs);
            udpRemote = AsioUdp::endpoint(peerAddr, static_cast<std::uint16_t>(params.port));

            connected.store(true);
            enterUdpRecoveryInternal();

            connectionLostSignaled.store(false);
            wsThread = std::thread([this] { wsIoLoop(); });
            udpThread = std::thread([this] { udpReadLoop(); });
            keepaliveThread = std::thread([this] { keepaliveLoop(); });
            return Result::Ok;
        } catch (const std::exception&) {
            closeSockets();
            joinThreads();
            return Result::Network;
        }
    }

    Result reconnectTeardownInternal() {
        connectionLostSignaled.store(false);
        disconnectStarted.store(false);
        disconnectEventEmitted.store(false);
        stopRequested.store(false);
        wsThreadCompletesTeardown.store(false);

        if (!transportFullyStopped.load()) {
            haltTransportInternal();
            waitUntilTransportStoppedInternal(3000);
        }
        // connect()'s prior wait may have set transportFullyStopped without running halt here;
        // haltTransportInternal() leaves io_context stopped unless we reset transport state.
        clearTransportState();

        welcome = WelcomeConfig{};
        seq.store(0);
        clientTxOpen.store(false);
        {
            std::scoped_lock lock(encodeMu, decodeMu);
            opus.reset();
            txPcmBuffer.clear();
        }
        endLocalTxIfNeeded();
        lastWsHeartbeatSentNs.store(0);

        {
            std::lock_guard<std::mutex> lg(stateMu);
            sessionState.receiving = false;
            sessionState.localTxActive = false;
            sessionState.connectionLost = false;
            sessionState.connected = false;
            sessionState.udpReady = false;
        }

        return Result::Ok;
    }

    Result reconnectConnectInternal(const ConnectParams& params, int timeoutMs) {
        stopRequested.store(false);
        const int connectTimeout =
            timeoutMs > 0 ? timeoutMs : kReconnectConnectTimeoutMs;
        return connectInternal(params, connectTimeout, true);
    }

    Result reconnectInternal(const ConnectParams& params) {
        if (reconnectTeardownInternal() != Result::Ok) {
            return Result::Network;
        }
        return reconnectConnectInternal(params, 0);
    }

    void haltTransportInternal() {
        stopRequested.store(true);
        connected.store(false);
        signalStopWorkers();
        closeSockets();
        ioc.stop();
    }

    void waitUntilTransportStoppedInternal(int timeoutMs) {
        const auto deadline = timeoutMs < 0
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (!transportFullyStopped.load()) {
            if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) {
                haltTransportInternal();
                break;
            }
            bool joinedTeardown = false;
            {
                std::unique_lock<std::mutex> lock(teardownThreadMu_);
                if (teardownThread_.joinable()) {
                    joinTeardownThreadLocked(lock);
                    joinedTeardown = true;
                }
            }
            if (transportFullyStopped.load()) {
                break;
            }
            // No background teardown worker — keep polling until deadline (bounded wait)
            // or until transportFullyStopped is set by a worker exit path.
            if (!joinedTeardown && timeoutMs < 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::unique_lock<std::mutex> lock(teardownThreadMu_);
            if (teardownThread_.joinable()) {
                if (timeoutMs < 0 || std::chrono::steady_clock::now() < deadline) {
                    joinTeardownThreadLocked(lock);
                } else {
                    lock.unlock();
                    teardownThread_.detach();
                    lock.lock();
                }
            }
        }

        const bool pastDeadline =
            timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline;
        if (pastDeadline) {
            haltTransportInternal();
            detachWorkerThreadsUnlessCaller();
            clearTransportState();
        } else {
            joinWsThreadUnlessCaller();
            joinUdpKeepaliveThreadsUnlessCaller();
        }
        transportFullyStopped.store(true);
    }

    void disconnectInternal() {
        bool expected = false;
        if (!disconnectStarted.compare_exchange_strong(expected, true)) {
            haltTransportInternal();
            return;
        }

        transportFullyStopped.store(false);
        haltTransportInternal();
        emitDisconnectedOnce();

        if (onWsThread()) {
            wsThreadCompletesTeardown.store(true);
            return;
        }

        std::lock_guard<std::mutex> lock(teardownThreadMu_);
        if (teardownThread_.joinable()) {
            // Never join on the UI/app thread; prior teardown continues in background.
            teardownThread_.detach();
        }
        teardownThread_ = std::thread([this] { finalizeTransportTeardown(); });
    }

    void flushTxPcmBuffer() {
        for (;;) {
            std::vector<uint8_t> encoded;
            bool haveFrame = false;
            {
                std::lock_guard<std::mutex> lg(encodeMu);
                const int frame = opus.frameSamples();
                if (frame <= 0) {
                    return;
                }
                if (!txPcmBuffer.empty() && static_cast<int>(txPcmBuffer.size()) < frame) {
                    txPcmBuffer.resize(static_cast<size_t>(frame), 0);
                }
                if (static_cast<int>(txPcmBuffer.size()) < frame) {
                    break;
                }
                const auto frameSpan = std::span<const int16_t>(txPcmBuffer.data(), static_cast<size_t>(frame));
                if (opus.encode(frameSpan, encoded) != Result::Ok) {
                    txPcmBuffer.clear();
                    return;
                }
                txPcmBuffer.erase(txPcmBuffer.begin(), txPcmBuffer.begin() + frame);
                haveFrame = true;
            }
            if (!haveFrame) {
                break;
            }
            sendOpusFrame(encoded.data(), encoded.size(), uplinkSignalByte());
        }
    }

    Result txStartInternal() {
        if (!connected.load() || welcome.sessionId == 0) {
            return Result::NotConnected;
        }
        {
            std::lock_guard<std::mutex> lg(encodeMu);
            txPcmBuffer.clear();
        }
        clientTxOpen.store(true);
        beginLocalTxIfNeeded();
        return Result::Ok;
    }

    Result txEndInternal() {
        if (!connected.load()) {
            return Result::NotConnected;
        }
        if (!clientTxOpen.load()) {
            return Result::NotReady;
        }
        flushTxPcmBuffer();
        {
            std::lock_guard<std::mutex> lg(encodeMu);
            txPcmBuffer.clear();
        }
        clientTxOpen.store(false);
        return sendTxEofBurstInternal();
    }

    Result feedTxPcmInternal(std::span<const int16_t> samples) {
        if (!connected.load() || welcome.sessionId == 0) {
            return Result::NotConnected;
        }
        if (!clientTxOpen.load()) {
            return Result::NotReady;
        }
        if (samples.empty()) {
            return Result::InvalidArg;
        }
        std::vector<uint8_t> encoded;
        bool haveFrame = false;
        {
            std::lock_guard<std::mutex> lg(encodeMu);
            const int frame = opus.frameSamples();
            if (frame <= 0) {
                return Result::NotReady;
            }
            txPcmBuffer.insert(txPcmBuffer.end(), samples.begin(), samples.end());
            if (static_cast<int>(txPcmBuffer.size()) < frame) {
                return Result::Ok;
            }
            const auto frameSpan = std::span<const int16_t>(txPcmBuffer.data(), static_cast<size_t>(frame));
            if (opus.encode(frameSpan, encoded) != Result::Ok) {
                txPcmBuffer.clear();
                return Result::NotReady;
            }
            txPcmBuffer.erase(txPcmBuffer.begin(), txPcmBuffer.begin() + frame);
            haveFrame = true;
        }
        if (haveFrame) {
            sendOpusFrame(encoded.data(), encoded.size(), uplinkSignalByte());
        }
        return Result::Ok;
    }

    Result sendTxOpusInternal(std::span<const uint8_t> opus) {
        if (!connected.load() || welcome.sessionId == 0) {
            return Result::NotConnected;
        }
        if (!clientTxOpen.load()) {
            return Result::NotReady;
        }
        if (opus.empty()) {
            return Result::InvalidArg;
        }
        sendOpusFrame(opus.data(), opus.size(), uplinkSignalByte());
        return Result::Ok;
    }

    Result sendTxEofBurstInternal() {
        if (!connected.load()) {
            return Result::NotConnected;
        }
        {
            std::lock_guard<std::mutex> lg(encodeMu);
            txPcmBuffer.clear();
        }
        static constexpr int delays[] = {0, 20, 60};
        for (int d : delays) {
            if (d > 0) {
                interruptibleSleepMs(d);
            }
            if (stopRequested.load()) {
                break;
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
        udpResetGraceUntilNs.store(nowNs() + 300 * kNsPerMs);
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
                setSocketRecvTimeoutMs(*udpSocket, kSocketRecvTimeoutMs);
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
    impl_->waitUntilTransportStoppedInternal(5000);
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

void Session::haltTransport() {
    impl_->haltTransportInternal();
}

void Session::waitUntilTransportStopped(int timeoutMs) {
    impl_->waitUntilTransportStoppedInternal(timeoutMs);
}

bool Session::isConnected() const {
    return impl_->connected.load();
}

bool Session::isSessionReady() const {
    return impl_->connected.load() && impl_->welcome.sessionId != 0;
}

bool Session::isConnectionLost() const {
    std::lock_guard<std::mutex> lg(impl_->stateMu);
    return impl_->sessionState.connectionLost;
}

Result Session::reconnectTeardown() {
    return impl_->reconnectTeardownInternal();
}

Result Session::reconnectConnect(const ConnectParams& params) {
    return impl_->reconnectConnectInternal(params, 0);
}

Result Session::reconnectConnect(const ConnectParams& params, int timeoutMs) {
    return impl_->reconnectConnectInternal(params, timeoutMs);
}

void Session::emitConnectionLost(const std::string& reason) {
    impl_->emitConnectionLostInternal(reason);
}

void Session::emitConnectionFailed(const std::string& reason) {
    impl_->emitConnectionFailedInternal(reason);
}

void Session::clearConnectionLostSignaled() {
    impl_->connectionLostSignaled.store(false);
}

void Session::cancelOngoingConnect() {
    impl_->cancelOngoingConnectInternal();
}

Result Session::submitTx(
    TxSubmitOp op,
    std::span<const int16_t> pcm,
    std::span<const uint8_t> opus) {
    return impl_->submitTxInternal(op, pcm, opus);
}

bool Session::waitTxQueueIdle(int timeoutMs) {
    return impl_->waitTxQueueIdleInternal(timeoutMs);
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

Result Session::punchUdpNat() {
    return impl_->punchUdpNatInternal();
}

SessionState Session::state() const {
    std::lock_guard<std::mutex> lg(impl_->stateMu);
    return impl_->sessionState;
}

} // namespace owalkie
