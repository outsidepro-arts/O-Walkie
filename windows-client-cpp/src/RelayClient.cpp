#include "RelayClient.h"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace ws = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;
using udp = boost::asio::ip::udp;

static int64_t nowNs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

RelayClient::RelayClient() : resolver_(ioc_) {}

RelayClient::~RelayClient() { Disconnect(); }

void RelayClient::CloseSocketsUnblockReaders() {
    try {
        if (ws_ && ws_->is_open()) {
            ws_->close(ws::close_code::abnormal);
        }
    } catch (...) {}
    try {
        if (udp_) {
            udp_->close();
        }
    } catch (...) {}
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
    connectionLostPosted_.store(false);
}

bool RelayClient::Connect(const std::string& host, int wsPort, int udpPort, const std::string& channel, bool repeater) {
    if (connected_.load()) {
        return true;
    }

    JoinWorkerThreads();

    host_ = host;
    wsPort_ = wsPort;
    udpPort_ = udpPort;
    channel_ = channel.empty() ? "global" : channel;
    repeater_ = repeater;
    stopRequested_.store(false);
    seq_.store(0);
    cfg_ = WelcomeConfig{};
    connectionLostPosted_.store(false);

    try {
        auto const results = resolver_.resolve(host_, std::to_string(wsPort_));
        ws_.emplace(ioc_);
        boost::asio::connect(ws_->next_layer(), results.begin(), results.end());
        ws_->handshake(host_, "/ws");

        udp_.emplace(ioc_, udp::endpoint(udp::v4(), 0));
        udpRemote_ = udp::endpoint(boost::asio::ip::make_address(host_), udpPort_);

        connected_.store(true);
        autoReconnectDesired_.store(true);
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
        autoReconnectDesired_.store(false);
        if (onStatus_) {
            onStatus_(std::string("Connect failed: ") + ex.what());
        }
        CloseSocketsUnblockReaders();
        JoinWorkerThreads();
        return false;
    }
}

void RelayClient::Disconnect() {
    stopRequested_.store(true);
    autoReconnectDesired_.store(false);
    connected_.store(false);
    CloseSocketsUnblockReaders();
    JoinWorkerThreads();
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
    std::lock_guard<std::mutex> lg(stateMu_);
    ws_->write(boost::asio::buffer(text));
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
    payload[8] = 255;
    try {
        udp_->send_to(boost::asio::buffer(payload), udpRemote_);
        lastOutboundNs_.store(nowNs());
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
    try {
        while (connected_.load() && !stopRequested_.load()) {
            boost::beast::flat_buffer buffer;
            ws_->read(buffer);
            auto text = boost::beast::buffers_to_string(buffer.data());
            HandleWsText(text);
        }
    } catch (const std::exception& ex) {
        connected_.store(false);
        CloseSocketsUnblockReaders();
        if (!stopRequested_.load()) {
            if (onStatus_) {
                onStatus_(std::string("WS ended: ") + ex.what());
            }
            PostConnectionLostOnce();
        }
    }
}

void RelayClient::UdpReadLoop() {
    try {
        std::array<uint8_t, 1500> data{};
        while (connected_.load() && !stopRequested_.load()) {
            udp::endpoint ep;
            std::size_t n = udp_->receive_from(boost::asio::buffer(data), ep);
            if (n <= 9) {
                continue;
            }
            lastInboundNs_.store(nowNs());
            std::vector<uint8_t> opus(n - 9);
            std::memcpy(opus.data(), data.data() + 9, n - 9);
            if (onOpusFrame_) {
                onOpusFrame_(opus);
            }
        }
    } catch (...) {
        connected_.store(false);
        CloseSocketsUnblockReaders();
        if (!stopRequested_.load()) {
            if (onStatus_) {
                onStatus_("UDP ended");
            }
            PostConnectionLostOnce();
        }
    }
}

void RelayClient::KeepaliveLoop() {
    std::mt19937 rng(std::random_device{}());
    while (connected_.load() && !stopRequested_.load()) {
        auto baseMs = 5000;
        auto jitter = baseMs * 15 / 100;
        std::uniform_int_distribution<int> dist(baseMs - jitter, baseMs + jitter);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));

        auto now = nowNs();
        auto lastTraffic = std::max(lastInboundNs_.load(), lastOutboundNs_.load());
        if (lastTraffic == 0 || (now - lastTraffic) >= 5'000'000'000LL) {
            SendUdpKeepalive();
        }
    }
}
