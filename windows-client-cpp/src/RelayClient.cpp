#include "RelayClient.h"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace ws = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;
using udp = boost::asio::ip::udp;
namespace {
constexpr uint8_t kUdpKeepaliveSignal = 255;
constexpr uint8_t kUdpKeepaliveAckSignal = 254;
constexpr int64_t kUdpKeepaliveIntervalNs = 5'000'000'000LL;
constexpr int64_t kUdpKeepaliveRtxNs = 1'000'000'000LL;
constexpr int64_t kUdpKeepaliveLostNs = 8'000'000'000LL;
}

static int64_t nowNs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

RelayClient::RelayClient() : resolver_(ioc_) {}

RelayClient::~RelayClient() { Disconnect(); }

void RelayClient::CloseSocketsUnblockReaders() {
    boost::system::error_code ec;
    // Unblock threads stuck in blocking udp::receive_from / ws::read before close.
    // Closing the websocket from another thread while read() is active can trigger
    // Beast debug assertions and leave the UI thread blocked in join().
    try {
        if (udp_) {
            udp_->cancel(ec);
        }
    } catch (...) {}
    ec.clear();
    try {
        if (ws_) {
            ws_->next_layer().cancel(ec);
        }
    } catch (...) {}
    ec.clear();
    try {
        if (ws_ && ws_->is_open()) {
            ws_->close(ws::close_code::going_away, ec);
        }
    } catch (...) {}
    ec.clear();
    try {
        if (udp_) {
            udp_->close(ec);
        }
    } catch (...) {}
}

void RelayClient::SetRepeaterMode(bool enabled) {
    repeater_ = enabled;
    json j;
    j["type"] = "repeater_mode";
    j["enabled"] = enabled;
    SendWsJson(j.dump());
}

void RelayClient::PostConnectionLostOnce() {
    if (stopRequested_.load() || !autoReconnectDesired_.load()) {
        return;
    }
    bool expected = false;
    if (connectionLostPosted_.compare_exchange_strong(expected, true)) {
        if (onConnectionLost_) {
            onConnectionLost_();
        }
    }
}

void RelayClient::JoinWorkerThreads() {
    const auto tid = std::this_thread::get_id();
    if (wsThread_.joinable() && wsThread_.get_id() != tid) {
        wsThread_.join();
    }
    if (udpThread_.joinable() && udpThread_.get_id() != tid) {
        udpThread_.join();
    }
    if (keepaliveThread_.joinable() && keepaliveThread_.get_id() != tid) {
        keepaliveThread_.join();
    }
    ws_.reset();
    udp_.reset();
    connected_.store(false);
    lastUdpKeepaliveSentNs_.store(0);
    udpKeepalivePendingSinceNs_.store(0);
}

bool RelayClient::Connect(const std::string& host, int serverPort, const std::string& channel, bool repeater) {
    if (connected_.load()) {
        return true;
    }

    // Force-unblock stale reader threads from previous broken session before join.
    // Otherwise JoinWorkerThreads() can hang and prevent reconnect attempts.
    CloseSocketsUnblockReaders();
    JoinWorkerThreads();

    host_ = host;
    serverPort_ = serverPort;
    channel_ = channel.empty() ? "global" : channel;
    repeater_ = repeater;
    stopRequested_.store(false);
    // Any outbound Connect (UI or reconnect timer) means we want auto-retry after drops
    // until explicit Disconnect(). Welcome protocol errors clear this flag again.
    autoReconnectDesired_.store(true);
    seq_.store(0);
    cfg_ = WelcomeConfig{};
    connectionLostPosted_.store(false);

    try {
        auto const results = resolver_.resolve(host_, std::to_string(serverPort_));
        ws_.emplace(ioc_);
        boost::asio::connect(ws_->next_layer(), results.begin(), results.end());
        ws_->handshake(host_, "/ws");
        {
            const tcp::endpoint tcpRemote = ws_->next_layer().remote_endpoint();
            const boost::asio::ip::address peerAddr = tcpRemote.address();
            if (peerAddr.is_v4()) {
                udp_.emplace(ioc_, udp::endpoint(udp::v4(), 0));
            } else {
                udp_.emplace(ioc_, udp::endpoint(udp::v6(), 0));
            }
            udpRemote_ = udp::endpoint(peerAddr, static_cast<std::uint16_t>(serverPort_));
        }

        connected_.store(true);
        if (onConnected_) {
            onConnected_(true);
        }
        if (onStatus_) {
            onStatus_("Connected");
        }

        wsThread_ = std::thread([this] { WsReadLoop(); });
        udpThread_ = std::thread([this] { UdpReadLoop(); });
        keepaliveThread_ = std::thread([this] { KeepaliveLoop(); });
        return true;
    } catch (const std::exception& ex) {
        if (onStatus_) {
            onStatus_(std::string("Connect failed: ") + ex.what());
        }
        CloseSocketsUnblockReaders();
        JoinWorkerThreads();
        // Keep autoReconnectDesired_ unchanged so MainFrame retry timer keeps backing off
        // until the server is reachable again (only Disconnect clears the flag).
        return false;
    }
}

void RelayClient::Disconnect() {
    stopRequested_.store(true);
    autoReconnectDesired_.store(false);
    connected_.store(false);
    CloseSocketsUnblockReaders();
    JoinWorkerThreads();
    connectionLostPosted_.store(false);
    if (onConnected_) {
        onConnected_(false);
    }
}

WelcomeConfig RelayClient::CurrentConfig() const {
    std::lock_guard<std::mutex> lg(stateMu_);
    return cfg_;
}

void RelayClient::SendWsJson(const std::string& text) {
    if (!ws_ || !connected_.load()) {
        return;
    }
    boost::system::error_code ec;
    std::lock_guard<std::mutex> lg(stateMu_);
    if (!ws_ || !ws_->is_open()) {
        return;
    }
    ws_->write(boost::asio::buffer(text), ec);
}

void RelayClient::SendOpusFrame(const uint8_t* data, size_t size, uint8_t signal) {
    if (!udp_ || !connected_.load() || cfg_.sessionId == 0 || size == 0) {
        return;
    }

    int seq = seq_.fetch_add(1) + 1;
    std::vector<uint8_t> payload(9 + size, 0);
    payload[0] = static_cast<uint8_t>((cfg_.sessionId >> 24) & 0xFF);
    payload[1] = static_cast<uint8_t>((cfg_.sessionId >> 16) & 0xFF);
    payload[2] = static_cast<uint8_t>((cfg_.sessionId >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>((cfg_.sessionId) & 0xFF);
    payload[4] = static_cast<uint8_t>((seq >> 24) & 0xFF);
    payload[5] = static_cast<uint8_t>((seq >> 16) & 0xFF);
    payload[6] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    payload[7] = static_cast<uint8_t>((seq) & 0xFF);
    payload[8] = signal;
    std::memcpy(payload.data() + 9, data, size);

    try {
        udp_->send_to(boost::asio::buffer(payload), udpRemote_);
        lastOutboundNs_.store(nowNs());
    } catch (...) {}
}

void RelayClient::SendUdpKeepalive() {
    if (!udp_ || !connected_.load() || cfg_.sessionId == 0) {
        return;
    }
    std::array<uint8_t, 9> payload{};
    payload[0] = static_cast<uint8_t>((cfg_.sessionId >> 24) & 0xFF);
    payload[1] = static_cast<uint8_t>((cfg_.sessionId >> 16) & 0xFF);
    payload[2] = static_cast<uint8_t>((cfg_.sessionId >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>((cfg_.sessionId) & 0xFF);
    payload[8] = kUdpKeepaliveSignal;
    try {
        udp_->send_to(boost::asio::buffer(payload), udpRemote_);
        const int64_t now = nowNs();
        lastOutboundNs_.store(now);
        lastUdpKeepaliveSentNs_.store(now);
        int64_t expected = 0;
        (void)udpKeepalivePendingSinceNs_.compare_exchange_strong(expected, now);
    } catch (...) {}
}

void RelayClient::SendUdpTxEof() {
    if (!udp_ || !connected_.load() || cfg_.sessionId == 0) {
        return;
    }
    int seq = seq_.fetch_add(1) + 1;
    std::array<uint8_t, 9> payload{};
    payload[0] = static_cast<uint8_t>((cfg_.sessionId >> 24) & 0xFF);
    payload[1] = static_cast<uint8_t>((cfg_.sessionId >> 16) & 0xFF);
    payload[2] = static_cast<uint8_t>((cfg_.sessionId >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>((cfg_.sessionId) & 0xFF);
    payload[4] = static_cast<uint8_t>((seq >> 24) & 0xFF);
    payload[5] = static_cast<uint8_t>((seq >> 16) & 0xFF);
    payload[6] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    payload[7] = static_cast<uint8_t>((seq) & 0xFF);
    payload[8] = 0;
    try {
        udp_->send_to(boost::asio::buffer(payload), udpRemote_);
        lastOutboundNs_.store(nowNs());
    } catch (...) {}
}

void RelayClient::SendTxEofBurst() {
    static constexpr int delays[] = {0, 20, 60};
    for (int d : delays) {
        if (d > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(d));
        }
        SendUdpTxEof();
    }
}

int RelayClient::NormalizeSampleRate(int v) {
    switch (v) {
        case 8000:
        case 12000:
        case 16000:
        case 24000:
        case 48000:
            return v;
        default:
            return 8000;
    }
}

int RelayClient::NormalizePacketMs(int v) {
    switch (v) {
        case 10:
        case 20:
        case 40:
        case 60:
            return v;
        default:
            return 20;
    }
}

void RelayClient::HandleWsText(const std::string& text) {
    try {
        auto root = json::parse(text);
        auto type = root.value("type", std::string{});
        if (type == "welcome") {
            int protocol = root.value("protocolVersion", -1);
            if (protocol != 2) {
                if (onStatus_) {
                    onStatus_("Protocol mismatch");
                }
                autoReconnectDesired_.store(false);
                connected_.store(false);
                CloseSocketsUnblockReaders();
                if (onConnected_) {
                    onConnected_(false);
                }
                return;
            }
            if (!root.contains("sampleRate")) {
                if (onStatus_) {
                    onStatus_("Missing sampleRate in welcome");
                }
                autoReconnectDesired_.store(false);
                connected_.store(false);
                CloseSocketsUnblockReaders();
                if (onConnected_) {
                    onConnected_(false);
                }
                return;
            }
            WelcomeConfig local{};
            local.sessionId = root.value("sessionId", 0u);
            local.sampleRate = NormalizeSampleRate(root.value("sampleRate", 8000));
            local.packetMs = NormalizePacketMs(root.value("packetMs", 20));
            local.busyMode = root.value("busyMode", false);
            if (root.contains("opus") && root["opus"].is_object()) {
                auto opus = root["opus"];
                local.bitrate = std::clamp(opus.value("bitrate", 12000), 6000, 510000);
                local.complexity = std::clamp(opus.value("complexity", 5), 0, 10);
                local.fec = opus.value("fec", true);
                local.dtx = opus.value("dtx", false);
                local.application = opus.value("application", "voip");
            }
            {
                std::lock_guard<std::mutex> lg(stateMu_);
                cfg_ = local;
            }
            if (onWelcome_) {
                onWelcome_(local);
            }

            json join = {{"type", "join"}, {"channel", channel_}};
            SendWsJson(join.dump());

            int localPort = udp_ ? udp_->local_endpoint().port() : 0;
            json udpHello = {{"type", "udp_hello"}, {"udpPort", localPort}};
            SendWsJson(udpHello.dump());

            json rep = {{"type", "repeater_mode"}, {"enabled", repeater_}};
            SendWsJson(rep.dump());

            SendUdpKeepalive();
            if (onStatus_) {
                onStatus_("Welcome received");
            }
        } else if (type == "tx_stop") {
            if (onTxStop_) {
                onTxStop_();
            }
        }
    } catch (...) {
        if (onStatus_) {
            onStatus_("WS parse error");
        }
    }
}

void RelayClient::WsReadLoop() {
    boost::system::error_code ec;
    while (connected_.load() && !stopRequested_.load()) {
        if (!ws_ || !ws_->is_open()) {
            break;
        }
        boost::beast::flat_buffer buffer;
        ws_->read(buffer, ec);
        if (ec) {
            connected_.store(false);
            if (!stopRequested_.load()) {
                CloseSocketsUnblockReaders();
                if (onStatus_) {
                    onStatus_(std::string("WS ended: ") + ec.message());
                }
                if (onConnected_) {
                    onConnected_(false);
                }
                PostConnectionLostOnce();
            }
            return;
        }
        const auto text = boost::beast::buffers_to_string(buffer.data());
        HandleWsText(text);
    }
}

void RelayClient::UdpReadLoop() {
    boost::system::error_code ec;
    std::array<uint8_t, 1500> data{};
    while (connected_.load() && !stopRequested_.load()) {
        if (!udp_) {
            break;
        }
        udp::endpoint ep;
        const std::size_t n = udp_->receive_from(boost::asio::buffer(data), ep, 0, ec);
        if (ec) {
            connected_.store(false);
            if (!stopRequested_.load()) {
                CloseSocketsUnblockReaders();
                if (onStatus_) {
                    onStatus_(std::string("UDP ended: ") + ec.message());
                }
                if (onConnected_) {
                    onConnected_(false);
                }
                PostConnectionLostOnce();
            }
            return;
        }
        if (n >= 9) {
            const uint32_t sid = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
            const uint32_t seq = (static_cast<uint32_t>(data[4]) << 24) | (static_cast<uint32_t>(data[5]) << 16) |
                (static_cast<uint32_t>(data[6]) << 8) | static_cast<uint32_t>(data[7]);
            const uint8_t signal = data[8];
            if (sid == cfg_.sessionId && seq == 0 && signal == kUdpKeepaliveAckSignal) {
                lastInboundNs_.store(nowNs());
                udpKeepalivePendingSinceNs_.store(0);
                continue;
            }
        }
        if (n <= 9) {
            continue;
        }
        lastInboundNs_.store(nowNs());
        udpKeepalivePendingSinceNs_.store(0);
        std::vector<uint8_t> opus(n - 9);
        std::memcpy(opus.data(), data.data() + 9, n - 9);
        if (onOpusFrame_) {
            onOpusFrame_(opus);
        }
    }
}

void RelayClient::KeepaliveLoop() {
    std::mt19937 rng(std::random_device{}());
    while (connected_.load() && !stopRequested_.load()) {
        auto baseMs = 1000;
        auto jitter = baseMs * 15 / 100;
        std::uniform_int_distribution<int> dist(baseMs - jitter, baseMs + jitter);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));

        const int64_t now = nowNs();
        const int64_t pendingSince = udpKeepalivePendingSinceNs_.load();
        if (pendingSince != 0) {
            if ((now - pendingSince) >= kUdpKeepaliveLostNs) {
                if (onStatus_) {
                    onStatus_("UDP keepalive timeout");
                }
                connected_.store(false);
                CloseSocketsUnblockReaders();
                if (!stopRequested_.load()) {
                    if (onConnected_) {
                        onConnected_(false);
                    }
                    PostConnectionLostOnce();
                }
                return;
            }
            const int64_t lastSent = lastUdpKeepaliveSentNs_.load();
            if (lastSent == 0 || (now - lastSent) >= kUdpKeepaliveRtxNs) {
                SendUdpKeepalive();
            }
            continue;
        }

        const int64_t lastSent = lastUdpKeepaliveSentNs_.load();
        if (lastSent == 0 || (now - lastSent) >= kUdpKeepaliveIntervalNs) {
            SendUdpKeepalive();
        }
    }
}
