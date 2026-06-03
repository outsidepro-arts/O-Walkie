#include "owalkie/json.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "owalkie/protocol.hpp"

namespace owalkie::json {
namespace {

using nlohmann::json;

Result failProtocol() { return Result::Protocol; }

} // namespace

Result parseWelcome(std::string_view text, WelcomeConfig& out, int requiredProtocolVersion) {
    try {
        const json root = json::parse(text);
        const int protocol = root.value("protocolVersion", -1);
        if (protocol != requiredProtocolVersion) {
            return failProtocol();
        }
        if (!root.contains("sampleRate")) {
            return failProtocol();
        }

        out = WelcomeConfig{};
        out.sessionId = root.value("sessionId", 0u);
        out.protocolVersion = protocol;
        out.sampleRate = owalkie::protocol::normalizeSampleRate(root.value("sampleRate", 8000));
        out.packetMs = owalkie::protocol::normalizePacketMs(root.value("packetMs", 20));
        out.busyMode = root.value("busyMode", false);

        int tt = 60;
        if (root.contains("transmitTimeoutSec") && !root["transmitTimeoutSec"].is_null()) {
            tt = root.value("transmitTimeoutSec", 60);
        } else if (root.contains("transmit_timeout") && !root["transmit_timeout"].is_null()) {
            tt = root.value("transmit_timeout", 60);
        }
        out.transmitTimeoutSec = std::max(0, tt);

        if (root.contains("opus") && root["opus"].is_object()) {
            const auto& opus = root["opus"];
            out.opus.bitrate = std::clamp(opus.value("bitrate", 12000), 6000, 510000);
            out.opus.complexity = std::clamp(opus.value("complexity", 5), 0, 10);
            out.opus.fec = opus.value("fec", true);
            out.opus.dtx = opus.value("dtx", false);
            out.opus.application = opus.value("application", "voip");
        }
        return Result::Ok;
    } catch (...) {
        return failProtocol();
    }
}

Result parseServerMessage(std::string_view text, Event& out) {
    try {
        const json root = json::parse(text);
        const std::string type = root.value("type", std::string{});

        if (type == "welcome") {
            out.type = EventType::Welcome;
            return parseWelcome(text, out.welcome);
        }
        if (type == "rx_broadcast_start") {
            out.type = EventType::RxBroadcastStart;
            out.rxBusyMode = root.value("busyMode", false);
            return Result::Ok;
        }
        if (type == "rx_broadcast_end") {
            out.type = EventType::RxBroadcastEnd;
            out.rxEndDelayMs = root.value("endDelayMs", 0);
            return Result::Ok;
        }
        if (type == "ptt_lock") {
            out.type = EventType::PttLocked;
            out.pttDisplaySec = root.value("displaySec", 0);
            return Result::Ok;
        }
        if (type == "ptt_unlock") {
            out.type = EventType::PttUnlocked;
            return Result::Ok;
        }
        if (type == "tx_countdown_start") {
            out.type = EventType::TxCountdownStart;
            return Result::Ok;
        }
        if (type == "tx_stop") {
            out.type = EventType::TxStop;
            out.txStopInfo = root.value("info", std::string{"transmit_timeout_reached"});
            return Result::Ok;
        }
        if (type == "joined" || type == "pong" || type == "udp_registered" || type == "repeater_mode") {
            return Result::InvalidArg;
        }
        return Result::InvalidArg;
    } catch (...) {
        return Result::Protocol;
    }
}

std::string buildJoin(std::string_view channel) {
    json j = {{"type", "join"}, {"channel", std::string(channel)}};
    return j.dump();
}

std::string buildUdpHello(int localUdpPort) {
    json j = {{"type", "udp_hello"}, {"udpPort", localUdpPort}};
    return j.dump();
}

std::string buildRepeaterMode(bool enabled) {
    json j = {{"type", "repeater_mode"}, {"enabled", enabled}};
    return j.dump();
}

std::string buildHeartbeat() {
    json j = {{"type", "heartbeat"}};
    return j.dump();
}

} // namespace owalkie::json
