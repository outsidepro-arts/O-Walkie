#include "owalkie/activity_probe.hpp"

#include <algorithm>
#include <chrono>
#include <string>

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
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include "owalkie/json.hpp"

namespace owalkie::activity_probe {
namespace {

namespace ws = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;
using steady_clock = std::chrono::steady_clock;

constexpr int kDefaultTimeoutMs = 4000;
constexpr int kResolveTimeoutMs = 2500;
constexpr int kConnectTimeoutMs = 3500;
constexpr int kReadSliceMs = 400;

struct ParsedHost {
    std::string host;
    bool useTls = false;
};

void setSocketRecvTimeoutMs(tcp::socket& socket, int timeoutMs) {
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

bool isSocketRecvTimeout(const boost::system::error_code& ec) {
#if defined(_WIN32)
    return ec.value() == WSAETIMEDOUT;
#else
    return ec == boost::asio::error::timed_out;
#endif
}

ParsedHost parseHostEndpoint(const std::string& rawHost) {
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

    if (value.empty()) {
        return out;
    }

    if (value.front() == '[') {
        const auto closing = value.find(']');
        if (closing != std::string::npos) {
            out.host = value.substr(1, closing - 1);
        }
        return out;
    }

    const auto colonCount = std::count(value.begin(), value.end(), ':');
    if (colonCount == 1) {
        const auto idx = value.rfind(':');
        out.host = value.substr(0, idx);
    } else {
        out.host = value;
    }
    return out;
}

bool resolveWithTimeout(
    boost::asio::io_context& ioc,
    tcp::resolver& resolver,
    const std::string& host,
    const std::string& port,
    int timeoutMs,
    tcp::resolver::results_type& resultsOut) {
    bool completed = false;
    boost::system::error_code resolveEc;
    const auto deadline = steady_clock::now() + std::chrono::milliseconds(timeoutMs);

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

    while (!completed) {
        ioc.run_for(std::chrono::milliseconds(100));
        if (steady_clock::now() >= deadline) {
            resolver.cancel();
            break;
        }
    }
    if (!completed) {
        resolver.cancel();
        (void)ioc.run_for(std::chrono::milliseconds(100));
    }
    ioc.restart();
    return completed && !resolveEc;
}

bool connectTcpWithTimeout(
    boost::asio::io_context& ioc,
    tcp::socket& socket,
    const tcp::resolver::results_type& results,
    int timeoutMs) {
    boost::system::error_code connectEc = boost::asio::error::would_block;
    bool completed = false;
    boost::asio::steady_timer timer(ioc);
    const auto deadline = steady_clock::now() + std::chrono::milliseconds(timeoutMs);

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

    while (!completed) {
        ioc.run_for(std::chrono::milliseconds(50));
        if (steady_clock::now() >= deadline) {
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
    return completed && !connectEc;
}

bool handshakeWithTimeout(
    boost::asio::io_context& ioc,
    ws::stream<tcp::socket>& stream,
    const std::string& host,
    int timeoutMs) {
    boost::system::error_code ec = boost::asio::error::would_block;
    bool completed = false;
    boost::asio::steady_timer timer(ioc);
    const auto deadline = steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    timer.expires_after(std::chrono::milliseconds(timeoutMs));
    timer.async_wait([&](const boost::system::error_code& timerEc) {
        if (!timerEc && !completed) {
            boost::system::error_code cancelEc;
            stream.next_layer().cancel(cancelEc);
        }
    });

    stream.async_handshake(host, "/ws", [&](const boost::system::error_code& handshakeEc) {
        completed = true;
        (void)timer.cancel();
        ec = handshakeEc;
    });

    while (!completed) {
        ioc.run_for(std::chrono::milliseconds(50));
        if (steady_clock::now() >= deadline) {
            boost::system::error_code cancelEc;
            stream.next_layer().cancel(cancelEc);
            break;
        }
    }
    if (!completed) {
        boost::system::error_code cancelEc;
        stream.next_layer().cancel(cancelEc);
        (void)ioc.run_for(std::chrono::milliseconds(100));
    }
    ioc.restart();
    return completed && !ec;
}

bool readWsText(
    ws::stream<tcp::socket>& stream,
    std::string& out,
    steady_clock::time_point deadline) {
    boost::beast::flat_buffer buffer;
    while (steady_clock::now() < deadline) {
        const auto remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - steady_clock::now()).count());
        if (remainingMs <= 0) {
            break;
        }
        setSocketRecvTimeoutMs(stream.next_layer(), std::min(kReadSliceMs, remainingMs));
        boost::system::error_code ec;
        stream.read(buffer, ec);
        if (!ec) {
            out = boost::beast::buffers_to_string(buffer.data());
            return true;
        }
        if (isSocketRecvTimeout(ec)) {
            continue;
        }
        return false;
    }
    return false;
}

bool writeWsText(ws::stream<tcp::socket>& stream, const std::string& payload) {
    boost::system::error_code ec;
    stream.write(boost::asio::buffer(payload), ec);
    return !ec;
}

bool messageTypeIs(const std::string& text, const char* type) {
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        return root.value("type", std::string{}) == type;
    } catch (...) {
        return false;
    }
}

void closeWsQuietly(ws::stream<tcp::socket>& stream) {
    boost::system::error_code ec;
    stream.close(ws::close_code::normal, ec);
    boost::system::error_code ignored;
    stream.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
    stream.next_layer().close(ignored);
}

} // namespace

Result checkChannelActivity(const ConnectParams& params, int timeoutMs, bool& outActive) {
    outActive = false;
    if (params.host.empty() || params.port < 1 || params.port > 65535) {
        return Result::InvalidArg;
    }

    const ParsedHost parsed = parseHostEndpoint(params.host);
    if (parsed.host.empty()) {
        return Result::InvalidArg;
    }
    if (params.useTls || parsed.useTls) {
        return Result::Unsupported;
    }

    const std::string channel = params.channel.empty() ? "global" : params.channel;
    const int budgetMs = timeoutMs > 0 ? timeoutMs : kDefaultTimeoutMs;
    const auto deadline = steady_clock::now() + std::chrono::milliseconds(budgetMs);

    try {
        boost::asio::io_context ioc;
        tcp::resolver resolver(ioc);
        tcp::resolver::results_type results;
        if (!resolveWithTimeout(
                ioc,
                resolver,
                parsed.host,
                std::to_string(params.port),
                std::min(kResolveTimeoutMs, budgetMs),
                results)) {
            return Result::Network;
        }

        ws::stream<tcp::socket> stream(ioc);
        const int connectBudgetMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - steady_clock::now()).count());
        if (connectBudgetMs <= 0 ||
            !connectTcpWithTimeout(
                ioc, stream.next_layer(), results, std::min(kConnectTimeoutMs, connectBudgetMs))) {
            return Result::Network;
        }

        const int handshakeBudgetMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - steady_clock::now()).count());
        if (handshakeBudgetMs <= 0 ||
            !handshakeWithTimeout(ioc, stream, parsed.host, std::min(kConnectTimeoutMs, handshakeBudgetMs))) {
            closeWsQuietly(stream);
            return Result::Network;
        }

        setSocketRecvTimeoutMs(stream.next_layer(), 0);

        std::string message;
        if (!readWsText(stream, message, deadline) || !messageTypeIs(message, "welcome")) {
            closeWsQuietly(stream);
            return Result::Network;
        }

        if (!writeWsText(stream, json::buildHasActivity(channel))) {
            closeWsQuietly(stream);
            return Result::Network;
        }

        while (steady_clock::now() < deadline) {
            if (!readWsText(stream, message, deadline)) {
                break;
            }
            if (messageTypeIs(message, "has_activity")) {
                const Result parsed = json::parseHasActivityResponse(message, outActive);
                closeWsQuietly(stream);
                return parsed == Result::Ok ? Result::Ok : Result::Protocol;
            }
        }

        closeWsQuietly(stream);
        return Result::Network;
    } catch (...) {
        return Result::Network;
    }
}

} // namespace owalkie::activity_probe
