#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/beast/websocket.hpp>

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
};

class RelayClient {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using ConnectedCallback = std::function<void(bool)>;
    using WelcomeCallback = std::function<void(const WelcomeConfig&)>;
    using OpusFrameCallback = std::function<void(const std::vector<uint8_t>&)>;
    using TxStopCallback = std::function<void()>;
    using ConnectionLostCallback = std::function<void()>;

    RelayClient();
    ~RelayClient();

    bool Connect(const std::string& host, int serverPort, const std::string& channel, bool repeater);
    void Disconnect();
    void JoinWorkerThreads();

    bool IsConnected() const { return connected_.load(); }
    bool AutoReconnectDesired() const { return autoReconnectDesired_.load(); }
    WelcomeConfig CurrentConfig() const;

    void SendOpusFrame(const uint8_t* data, size_t size, uint8_t signal);
    void SendTxEofBurst();
    /// Updates server repeater flag when already connected (WS message).
    void SetRepeaterMode(bool enabled);

    void SetStatusCallback(StatusCallback cb) { onStatus_ = std::move(cb); }
    void SetConnectedCallback(ConnectedCallback cb) { onConnected_ = std::move(cb); }
    void SetWelcomeCallback(WelcomeCallback cb) { onWelcome_ = std::move(cb); }
    void SetOpusFrameCallback(OpusFrameCallback cb) { onOpusFrame_ = std::move(cb); }
    void SetTxStopCallback(TxStopCallback cb) { onTxStop_ = std::move(cb); }
    void SetConnectionLostCallback(ConnectionLostCallback cb) { onConnectionLost_ = std::move(cb); }

private:
    void WsReadLoop();
    void UdpReadLoop();
    void KeepaliveLoop();
    void HandleWsText(const std::string& text);
    void SendWsJson(const std::string& json);
    void SendUdpKeepalive();
    void SendUdpTxEof();
    void CloseSocketsUnblockReaders();
    void PostConnectionLostOnce();
    static int NormalizeSampleRate(int v);
    static int NormalizePacketMs(int v);

private:
    mutable std::mutex stateMu_;
    boost::asio::io_context ioc_;
    boost::asio::ip::tcp::resolver resolver_;
    std::optional<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>> ws_;
    std::optional<boost::asio::ip::udp::socket> udp_;
    boost::asio::ip::udp::endpoint udpRemote_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> autoReconnectDesired_{false};
    std::atomic<bool> connectionLostPosted_{false};
    std::atomic<int> seq_{0};
    WelcomeConfig cfg_;
    std::string host_;
    int serverPort_ = 0;
    std::string channel_;
    bool repeater_ = false;
    std::thread wsThread_;
    std::thread udpThread_;
    std::thread keepaliveThread_;
    std::atomic<int64_t> lastInboundNs_{0};
    std::atomic<int64_t> lastOutboundNs_{0};
    std::atomic<int64_t> lastUdpKeepaliveSentNs_{0};
    std::atomic<int64_t> udpKeepalivePendingSinceNs_{0};

    StatusCallback onStatus_;
    ConnectedCallback onConnected_;
    WelcomeCallback onWelcome_;
    OpusFrameCallback onOpusFrame_;
    TxStopCallback onTxStop_;
    ConnectionLostCallback onConnectionLost_;
};
