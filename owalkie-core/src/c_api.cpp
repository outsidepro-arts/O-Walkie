#include "owalkie_core.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "owalkie/json.hpp"
#include "owalkie/protocol.hpp"
#include "owalkie/session.hpp"
#include "owalkie/session_manager.hpp"
#include "owalkie/signal.hpp"
#include "owalkie/udp.hpp"

namespace {

owalkie_result toC(owalkie::Result r) {
    switch (r) {
        case owalkie::Result::Ok:
            return OWALKIE_OK;
        case owalkie::Result::InvalidArg:
            return OWALKIE_ERR_INVALID_ARG;
        case owalkie::Result::AlreadyConnected:
            return OWALKIE_ERR_ALREADY_CONNECTED;
        case owalkie::Result::NotConnected:
            return OWALKIE_ERR_NOT_CONNECTED;
        case owalkie::Result::Protocol:
            return OWALKIE_ERR_PROTOCOL;
        case owalkie::Result::Network:
            return OWALKIE_ERR_NETWORK;
        case owalkie::Result::Internal:
            return OWALKIE_ERR_INTERNAL;
        case owalkie::Result::Unsupported:
            return OWALKIE_ERR_UNSUPPORTED;
        case owalkie::Result::BufferTooSmall:
            return OWALKIE_ERR_BUFFER_TOO_SMALL;
        case owalkie::Result::NotReady:
            return OWALKIE_ERR_NOT_READY;
        case owalkie::Result::NoEvent:
            return OWALKIE_OK;
    }
    return OWALKIE_ERR_INTERNAL;
}

bool copyToBuf(const std::string& src, char* buf, size_t bufSize) {
    if (!buf || bufSize == 0) {
        return false;
    }
    if (src.size() + 1 > bufSize) {
        return false;
    }
    std::memcpy(buf, src.c_str(), src.size() + 1);
    return true;
}

void fillWelcomeC(
    const owalkie::WelcomeConfig& cfg,
    owalkie_welcome_config& out,
    std::string& opusAppScratch) {
    out.session_id = cfg.sessionId;
    out.protocol_version = cfg.protocolVersion;
    out.sample_rate = cfg.sampleRate;
    out.packet_ms = cfg.packetMs;
    out.opus_bitrate = cfg.opus.bitrate;
    out.opus_complexity = cfg.opus.complexity;
    out.opus_fec = cfg.opus.fec ? 1 : 0;
    out.opus_dtx = cfg.opus.dtx ? 1 : 0;
    opusAppScratch = cfg.opus.application;
    out.opus_application = opusAppScratch.c_str();
    out.busy_mode = cfg.busyMode ? 1 : 0;
    out.transmit_timeout_sec = cfg.transmitTimeoutSec;
}

owalkie_event_type toC(owalkie::EventType t) {
    switch (t) {
        case owalkie::EventType::Connecting:
            return OWALKIE_EV_CONNECTING;
        case owalkie::EventType::Connected:
            return OWALKIE_EV_CONNECTED;
        case owalkie::EventType::ConnectFailed:
            return OWALKIE_EV_CONNECT_FAILED;
        case owalkie::EventType::SessionReady:
            return OWALKIE_EV_SESSION_READY;
        case owalkie::EventType::Disconnected:
            return OWALKIE_EV_DISCONNECTED;
        case owalkie::EventType::ProtocolError:
            return OWALKIE_EV_PROTOCOL_ERROR;
        case owalkie::EventType::Welcome:
            return OWALKIE_EV_WELCOME;
        case owalkie::EventType::RxBroadcastStart:
            return OWALKIE_EV_RX_BROADCAST_START;
        case owalkie::EventType::RxBroadcastEnd:
            return OWALKIE_EV_RX_BROADCAST_END;
        case owalkie::EventType::LocalTxStart:
            return OWALKIE_EV_LOCAL_TX_START;
        case owalkie::EventType::LocalTxEnd:
            return OWALKIE_EV_LOCAL_TX_END;
        case owalkie::EventType::PttLocked:
            return OWALKIE_EV_PTT_LOCKED;
        case owalkie::EventType::PttUnlocked:
            return OWALKIE_EV_PTT_UNLOCKED;
        case owalkie::EventType::TxCountdownStart:
            return OWALKIE_EV_TX_COUNTDOWN_START;
        case owalkie::EventType::TxStop:
            return OWALKIE_EV_TX_STOP;
        case owalkie::EventType::UdpTransportReady:
            return OWALKIE_EV_UDP_TRANSPORT_READY;
        case owalkie::EventType::UdpTransportLost:
            return OWALKIE_EV_UDP_TRANSPORT_LOST;
    }
    return OWALKIE_EV_CONNECTING;
}

void fillManagedEventC(
    const owalkie::Event& ev,
    owalkie_event& cev,
    std::string& scratch) {
    std::memset(&cev, 0, sizeof(cev));
    cev.type = toC(ev.type);
    switch (ev.type) {
        case owalkie::EventType::Welcome:
        case owalkie::EventType::SessionReady:
            fillWelcomeC(ev.welcome, cev.u.welcome.config, scratch);
            break;
        case owalkie::EventType::RxBroadcastStart:
            cev.u.rx_broadcast_start.busy_mode = ev.rxBusyMode ? 1 : 0;
            break;
        case owalkie::EventType::RxBroadcastEnd:
            cev.u.rx_broadcast_end.end_delay_ms = ev.rxEndDelayMs;
            break;
        case owalkie::EventType::PttLocked:
            cev.u.ptt_locked.display_sec = ev.pttDisplaySec;
            break;
        case owalkie::EventType::TxStop:
            scratch = ev.txStopInfo;
            cev.u.tx_stop.info = scratch.c_str();
            break;
        case owalkie::EventType::ProtocolError:
            scratch = ev.protocolError;
            cev.u.protocol_error.message = scratch.c_str();
            break;
        case owalkie::EventType::ConnectFailed:
        case owalkie::EventType::Disconnected:
            cev.u.disconnected.code = ev.disconnectCode;
            scratch = ev.disconnectReason;
            cev.u.disconnected.reason = scratch.empty() ? nullptr : scratch.c_str();
            break;
        default:
            break;
    }
}

struct SessionWrapper {
    std::unique_ptr<owalkie::Session> session;
    owalkie_session_callbacks callbacks{};
    mutable std::mutex scratchMu;
    mutable std::string eventStringScratch;
    mutable std::string welcomeOpusScratch;
};

thread_local std::vector<uint8_t> g_udpUnpackScratch;

SessionWrapper* asWrapper(owalkie_session_t* session) {
    return reinterpret_cast<SessionWrapper*>(session);
}

const SessionWrapper* asWrapper(const owalkie_session_t* session) {
    return reinterpret_cast<const SessionWrapper*>(session);
}

} // namespace

int owalkie_normalize_sample_rate(int value) {
    return owalkie::protocol::normalizeSampleRate(value);
}

int owalkie_normalize_packet_ms(int value) {
    return owalkie::protocol::normalizePacketMs(value);
}

int owalkie_frame_samples(int sample_rate, int packet_ms) {
    return owalkie::protocol::frameSamples(sample_rate, packet_ms);
}

owalkie_result owalkie_json_parse_welcome(
    const char* json_text,
    size_t json_len,
    owalkie_welcome_config* out_config,
    char* opus_application_buf,
    size_t opus_application_buf_size) {
    if (!json_text || !out_config) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::WelcomeConfig cfg{};
    const auto text = std::string_view(json_text, json_len == 0 ? std::strlen(json_text) : json_len);
    const owalkie::Result r = owalkie::json::parseWelcome(text, cfg);
    if (r != owalkie::Result::Ok) {
        return toC(r);
    }
    std::string scratch;
    if (opus_application_buf && opus_application_buf_size > 0) {
        if (!copyToBuf(cfg.opus.application, opus_application_buf, opus_application_buf_size)) {
            return OWALKIE_ERR_BUFFER_TOO_SMALL;
        }
        fillWelcomeC(cfg, *out_config, scratch);
        out_config->opus_application = opus_application_buf;
    } else {
        fillWelcomeC(cfg, *out_config, scratch);
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_json_parse_server_message(
    const char* json_text,
    size_t json_len,
    owalkie_event* out_event,
    char* string_buf,
    size_t string_buf_size) {
    if (!json_text || !out_event) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::Event ev{};
    const auto text = std::string_view(json_text, json_len == 0 ? std::strlen(json_text) : json_len);
    const owalkie::Result r = owalkie::json::parseServerMessage(text, ev);
    if (r == owalkie::Result::NoEvent) {
        std::memset(out_event, 0, sizeof(*out_event));
        return OWALKIE_OK;
    }
    if (r != owalkie::Result::Ok) {
        return toC(r);
    }

    std::memset(out_event, 0, sizeof(*out_event));
    out_event->type = toC(ev.type);
    switch (ev.type) {
        case owalkie::EventType::Welcome: {
            std::string scratch;
            if (string_buf && string_buf_size > 0) {
                if (!copyToBuf(ev.welcome.opus.application, string_buf, string_buf_size)) {
                    return OWALKIE_ERR_BUFFER_TOO_SMALL;
                }
                fillWelcomeC(ev.welcome, out_event->u.welcome.config, scratch);
                out_event->u.welcome.config.opus_application = string_buf;
            } else {
                fillWelcomeC(ev.welcome, out_event->u.welcome.config, scratch);
            }
            break;
        }
        case owalkie::EventType::RxBroadcastStart:
            out_event->u.rx_broadcast_start.busy_mode = ev.rxBusyMode ? 1 : 0;
            break;
        case owalkie::EventType::RxBroadcastEnd:
            out_event->u.rx_broadcast_end.end_delay_ms = ev.rxEndDelayMs;
            break;
        case owalkie::EventType::PttLocked:
            out_event->u.ptt_locked.display_sec = ev.pttDisplaySec;
            break;
        case owalkie::EventType::TxStop:
            if (!string_buf || !copyToBuf(ev.txStopInfo, string_buf, string_buf_size)) {
                return OWALKIE_ERR_BUFFER_TOO_SMALL;
            }
            out_event->u.tx_stop.info = string_buf;
            break;
        case owalkie::EventType::ProtocolError:
            if (!string_buf || !copyToBuf(ev.protocolError, string_buf, string_buf_size)) {
                return OWALKIE_ERR_BUFFER_TOO_SMALL;
            }
            out_event->u.protocol_error.message = string_buf;
            break;
        case owalkie::EventType::Disconnected:
            out_event->u.disconnected.code = ev.disconnectCode;
            if (!ev.disconnectReason.empty()) {
                if (!string_buf || !copyToBuf(ev.disconnectReason, string_buf, string_buf_size)) {
                    return OWALKIE_ERR_BUFFER_TOO_SMALL;
                }
                out_event->u.disconnected.reason = string_buf;
            }
            break;
        default:
            break;
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_json_build_join(
    const char* channel,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written) {
    if (!channel || !out_buf || out_buf_size == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const std::string json = owalkie::json::buildJoin(channel);
    if (json.size() + 1 > out_buf_size) {
        return OWALKIE_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(out_buf, json.c_str(), json.size() + 1);
    if (out_written) {
        *out_written = json.size();
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_json_build_udp_hello(
    int local_udp_port,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written) {
    if (!out_buf || out_buf_size == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const std::string json = owalkie::json::buildUdpHello(local_udp_port);
    if (json.size() + 1 > out_buf_size) {
        return OWALKIE_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(out_buf, json.c_str(), json.size() + 1);
    if (out_written) {
        *out_written = json.size();
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_json_build_repeater_mode(
    int enabled,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written) {
    if (!out_buf || out_buf_size == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const std::string json = owalkie::json::buildRepeaterMode(enabled != 0);
    if (json.size() + 1 > out_buf_size) {
        return OWALKIE_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(out_buf, json.c_str(), json.size() + 1);
    if (out_written) {
        *out_written = json.size();
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_json_build_heartbeat(
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written) {
    if (!out_buf || out_buf_size == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const std::string json = owalkie::json::buildHeartbeat();
    if (json.size() + 1 > out_buf_size) {
        return OWALKIE_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(out_buf, json.c_str(), json.size() + 1);
    if (out_written) {
        *out_written = json.size();
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_signal_generate_pcm(
    const owalkie_signal_pattern* pattern,
    int sample_rate_hz,
    int16_t** out_samples,
    size_t* out_sample_count) {
    if (!pattern || !out_samples || !out_sample_count || !pattern->points || pattern->point_count == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::SignalPattern cpp{};
    cpp.points.reserve(pattern->point_count);
    for (size_t i = 0; i < pattern->point_count; ++i) {
        cpp.points.push_back(
            owalkie::SignalPoint{pattern->points[i].freq_hz, pattern->points[i].duration_ms});
    }
    cpp.tailMs = pattern->tail_ms;
    cpp.repeatCount = pattern->repeat_count;
    cpp.gain = pattern->gain;

    const std::vector<int16_t> pcm = owalkie::signal::generatePcm(cpp, sample_rate_hz);
    if (pcm.empty()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    auto* buf = static_cast<int16_t*>(std::malloc(pcm.size() * sizeof(int16_t)));
    if (!buf) {
        return OWALKIE_ERR_INTERNAL;
    }
    std::memcpy(buf, pcm.data(), pcm.size() * sizeof(int16_t));
    *out_samples = buf;
    *out_sample_count = pcm.size();
    return OWALKIE_OK;
}

void owalkie_signal_free_pcm(int16_t* samples) {
    std::free(samples);
}

owalkie_result owalkie_udp_pack(
    const owalkie_udp_audio_packet* packet,
    uint8_t* out_buf,
    size_t out_buf_size,
    size_t* out_len) {
    if (!packet || !out_buf) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::UdpAudioPacket cpp{};
    cpp.sessionId = packet->session_id;
    cpp.sequence = packet->sequence;
    cpp.signalStrength = packet->signal_strength;
    if (packet->opus && packet->opus_len > 0) {
        cpp.opus.assign(packet->opus, packet->opus + packet->opus_len);
    }
    std::vector<uint8_t> packed;
    const owalkie::Result r = owalkie::pkt::pack(cpp, packed);
    if (r != owalkie::Result::Ok) {
        return toC(r);
    }
    if (packed.size() > out_buf_size) {
        return OWALKIE_ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy(out_buf, packed.data(), packed.size());
    if (out_len) {
        *out_len = packed.size();
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_udp_unpack(
    const uint8_t* buf,
    size_t buf_len,
    owalkie_udp_audio_packet* out_packet) {
    if (!buf || !out_packet) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::UdpAudioPacket cpp{};
    const owalkie::Result r = owalkie::pkt::unpack(std::span<const uint8_t>(buf, buf_len), cpp);
    if (r != owalkie::Result::Ok) {
        return toC(r);
    }
    g_udpUnpackScratch = std::move(cpp.opus);
    out_packet->session_id = cpp.sessionId;
    out_packet->sequence = cpp.sequence;
    out_packet->signal_strength = cpp.signalStrength;
    out_packet->opus = g_udpUnpackScratch.empty() ? nullptr : g_udpUnpackScratch.data();
    out_packet->opus_len = g_udpUnpackScratch.size();
    return OWALKIE_OK;
}

int owalkie_udp_is_keepalive_signal(const uint8_t* buf, size_t len) {
    if (!buf) {
        return 0;
    }
    return owalkie::pkt::isKeepaliveSignal(std::span<const uint8_t>(buf, len)) ? 1 : 0;
}

int owalkie_udp_is_keepalive_ack(const uint8_t* buf, size_t len, uint32_t session_id) {
    if (!buf) {
        return 0;
    }
    return owalkie::pkt::isKeepaliveAck(std::span<const uint8_t>(buf, len), session_id) ? 1 : 0;
}

int owalkie_udp_is_tx_eof_marker(const uint8_t* buf, size_t len) {
    if (!buf) {
        return 0;
    }
    return owalkie::pkt::isTxEofMarker(std::span<const uint8_t>(buf, len)) ? 1 : 0;
}

#ifdef OWALKIE_CORE_HAS_SESSION

namespace {

struct ManagedBridge {
    owalkie_managed_callbacks cbs{};
    std::string eventScratch;
};

std::mutex g_managedBridgeMu;
std::unordered_map<owalkie::SessionId, std::shared_ptr<ManagedBridge>> g_managedBridges;

owalkie::SessionCallbacks makeManagedSessionCallbacks(const std::shared_ptr<ManagedBridge>& bridge) {
    owalkie::SessionCallbacks sessionCbs{};
    sessionCbs.onRxOpus = [bridge](std::span<const uint8_t> opus) {
        if (!bridge->cbs.on_rx_opus || opus.empty()) {
            return;
        }
        owalkie::SessionId sid = owalkie::kInvalidSessionId;
        {
            std::lock_guard<std::mutex> lock(g_managedBridgeMu);
            for (const auto& entry : g_managedBridges) {
                if (entry.second.get() == bridge.get()) {
                    sid = entry.first;
                    break;
                }
            }
        }
        if (sid == owalkie::kInvalidSessionId) {
            return;
        }
        bridge->cbs.on_rx_opus(
            bridge->cbs.user_data,
            sid,
            opus.data(),
            opus.size());
    };
    sessionCbs.onSessionEvent = [bridge](const owalkie::Event& ev) {
        if (!bridge->cbs.on_session_event) {
            return;
        }
        owalkie::SessionId sid = owalkie::kInvalidSessionId;
        {
            std::lock_guard<std::mutex> lock(g_managedBridgeMu);
            for (const auto& entry : g_managedBridges) {
                if (entry.second.get() == bridge.get()) {
                    sid = entry.first;
                    break;
                }
            }
        }
        if (sid == owalkie::kInvalidSessionId) {
            return;
        }
        owalkie_event cev{};
        fillManagedEventC(ev, cev, bridge->eventScratch);
        bridge->cbs.on_session_event(bridge->cbs.user_data, sid, &cev);
        if (ev.type == owalkie::EventType::ConnectFailed ||
            ev.type == owalkie::EventType::Disconnected ||
            ev.type == owalkie::EventType::ProtocolError) {
            std::lock_guard<std::mutex> lock(g_managedBridgeMu);
            g_managedBridges.erase(sid);
        }
    };
    return sessionCbs;
}

} // namespace

owalkie_session_id owalkie_connect(
    const owalkie_connect_params* params,
    const owalkie_managed_callbacks* callbacks) {
    if (!params || !params->host || !callbacks || !callbacks->on_session_event) {
        return owalkie_invalid_session_id();
    }

    auto bridge = std::make_shared<ManagedBridge>();
    bridge->cbs = *callbacks;

    owalkie::ConnectParams cpp{};
    cpp.host = params->host;
    cpp.port = params->port;
    cpp.channel = params->channel ? params->channel : "global";
    cpp.useTls = params->use_tls != 0;
    cpp.repeaterMode = params->repeater_mode != 0;

    const owalkie::SessionId id = owalkie::SessionManager::instance().connect(
        cpp,
        makeManagedSessionCallbacks(bridge),
        [bridge](owalkie::SessionId allocated) {
            std::lock_guard<std::mutex> lock(g_managedBridgeMu);
            g_managedBridges[allocated] = bridge;
        });
    if (id == owalkie::kInvalidSessionId) {
        return owalkie_invalid_session_id();
    }
    return id;
}

void owalkie_disconnect(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_managedBridgeMu);
        g_managedBridges.erase(session_id);
    }
    owalkie::SessionManager::instance().disconnect(session_id);
}

void owalkie_disconnect_all(void) {
    owalkie::SessionManager::instance().disconnectAll();
}

void owalkie_disconnect_all_and_wait(int timeout_ms) {
    owalkie::SessionManager::instance().disconnectAllAndWait(timeout_ms < 0 ? 3000 : timeout_ms);
}

int owalkie_session_id_valid(owalkie_session_id session_id) {
    return owalkie::SessionManager::instance().isValid(session_id) ? 1 : 0;
}

int owalkie_session_id_ready(owalkie_session_id session_id) {
    return owalkie::SessionManager::instance().isSessionReady(session_id) ? 1 : 0;
}

owalkie_result owalkie_send_tx_opus(
    owalkie_session_id session_id,
    const uint8_t* opus,
    size_t opus_len,
    int signal_strength) {
    if (session_id == owalkie_invalid_session_id() || !opus || opus_len == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().sendTxOpus(
        session_id,
        std::span<const uint8_t>(opus, opus_len),
        signal_strength));
}

owalkie_result owalkie_send_tx_eof_burst(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().sendTxEofBurst(session_id));
}

owalkie_result owalkie_set_repeater_mode(owalkie_session_id session_id, int enabled) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().setRepeaterMode(session_id, enabled != 0));
}

owalkie_result owalkie_set_tx_signal_strength(owalkie_session_id session_id, int strength_0_255) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().setTxSignalStrength(session_id, strength_0_255));
}

int owalkie_get_tx_signal_strength(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return 255;
    }
    return owalkie::SessionManager::instance().txSignalStrength(session_id);
}

void owalkie_set_power_profile(owalkie_session_id session_id, owalkie_power_profile profile) {
    if (session_id == owalkie_invalid_session_id()) {
        return;
    }
    owalkie::PowerProfile cpp = owalkie::PowerProfile::Foreground;
    switch (profile) {
        case OWALKIE_POWER_BACKGROUND:
            cpp = owalkie::PowerProfile::Background;
            break;
        case OWALKIE_POWER_ACTIVE_TX:
            cpp = owalkie::PowerProfile::ActiveTx;
            break;
        default:
            break;
    }
    owalkie::SessionManager::instance().setPowerProfile(session_id, cpp);
}

void owalkie_notify_network_changed(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return;
    }
    owalkie::SessionManager::instance().notifyNetworkChanged(session_id);
}

owalkie_result owalkie_reset_udp_transport(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().resetUdpTransport(session_id));
}

owalkie_result owalkie_session_create(owalkie_session_t** out_session) {
    if (!out_session) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    auto wrapper = std::make_unique<SessionWrapper>();
    std::unique_ptr<owalkie::Session> session;
    const owalkie::Result r = owalkie::Session::create(session);
    if (r != owalkie::Result::Ok) {
        return toC(r);
    }
    wrapper->session = std::move(session);
    *out_session = reinterpret_cast<owalkie_session_t*>(wrapper.release());
    return OWALKIE_OK;
}

void owalkie_session_destroy(owalkie_session_t* session) {
    delete reinterpret_cast<SessionWrapper*>(session);
}

void owalkie_session_set_callbacks(
    owalkie_session_t* session,
    const owalkie_session_callbacks* callbacks) {
    if (!session) {
        return;
    }
    auto* wrapper = reinterpret_cast<SessionWrapper*>(session);
    if (callbacks) {
        wrapper->callbacks = *callbacks;
    } else {
        wrapper->callbacks = {};
    }

    owalkie::SessionCallbacks cpp{};
    if (wrapper->callbacks.on_rx_pcm) {
        cpp.onRxPcm = [wrapper](std::span<const int16_t> pcm, int sampleRate, int packetMs) {
            wrapper->callbacks.on_rx_pcm(
                wrapper->callbacks.user_data,
                pcm.data(),
                pcm.size(),
                sampleRate,
                packetMs);
        };
    }
    if (wrapper->callbacks.on_session_event) {
        cpp.onSessionEvent = [wrapper](const owalkie::Event& ev) {
            owalkie_event cev{};
            std::memset(&cev, 0, sizeof(cev));
            cev.type = toC(ev.type);

            std::lock_guard<std::mutex> lg(wrapper->scratchMu);
            switch (ev.type) {
                case owalkie::EventType::Welcome:
                    fillWelcomeC(ev.welcome, cev.u.welcome.config, wrapper->welcomeOpusScratch);
                    break;
                case owalkie::EventType::RxBroadcastStart:
                    cev.u.rx_broadcast_start.busy_mode = ev.rxBusyMode ? 1 : 0;
                    break;
                case owalkie::EventType::RxBroadcastEnd:
                    cev.u.rx_broadcast_end.end_delay_ms = ev.rxEndDelayMs;
                    break;
                case owalkie::EventType::PttLocked:
                    cev.u.ptt_locked.display_sec = ev.pttDisplaySec;
                    break;
                case owalkie::EventType::TxStop:
                    wrapper->eventStringScratch = ev.txStopInfo;
                    cev.u.tx_stop.info = wrapper->eventStringScratch.c_str();
                    break;
                case owalkie::EventType::ProtocolError:
                    wrapper->eventStringScratch = ev.protocolError;
                    cev.u.protocol_error.message = wrapper->eventStringScratch.c_str();
                    break;
                case owalkie::EventType::Disconnected:
                    cev.u.disconnected.code = ev.disconnectCode;
                    wrapper->eventStringScratch = ev.disconnectReason;
                    cev.u.disconnected.reason = wrapper->eventStringScratch.empty()
                        ? nullptr
                        : wrapper->eventStringScratch.c_str();
                    break;
                default:
                    break;
            }
            wrapper->callbacks.on_session_event(wrapper->callbacks.user_data, &cev);
        };
    }
    wrapper->session->setCallbacks(std::move(cpp));
}

owalkie_result owalkie_session_connect(
    owalkie_session_t* session,
    const owalkie_connect_params* params) {
    if (!session || !params || !params->host) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::ConnectParams cpp{};
    cpp.host = params->host;
    cpp.port = params->port;
    cpp.channel = params->channel ? params->channel : "global";
    cpp.useTls = params->use_tls != 0;
    cpp.repeaterMode = params->repeater_mode != 0;
    return toC(asWrapper(session)->session->connect(cpp));
}

void owalkie_session_disconnect(owalkie_session_t* session) {
    auto* wrapper = asWrapper(session);
    if (wrapper && wrapper->session) {
        wrapper->session->disconnect();
    }
}

int owalkie_session_is_connected(const owalkie_session_t* session) {
    const auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return 0;
    }
    return wrapper->session->isConnected() ? 1 : 0;
}

void owalkie_session_set_auto_reconnect(owalkie_session_t* session, int enabled) {
    auto* wrapper = asWrapper(session);
    if (wrapper && wrapper->session) {
        wrapper->session->setAutoReconnect(enabled != 0);
    }
}

int owalkie_session_auto_reconnect_enabled(const owalkie_session_t* session) {
    const auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return 0;
    }
    return wrapper->session->autoReconnectEnabled() ? 1 : 0;
}

owalkie_result owalkie_session_feed_tx_pcm(
    owalkie_session_t* session,
    const int16_t* samples,
    size_t sample_count) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session || !samples || sample_count == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->feedTxPcm(std::span<const int16_t>(samples, sample_count)));
}

owalkie_result owalkie_session_send_tx_opus(
    owalkie_session_t* session,
    const uint8_t* opus,
    size_t opus_len,
    int signal_strength) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session || !opus || opus_len == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->sendTxOpus(std::span<const uint8_t>(opus, opus_len), signal_strength));
}

owalkie_result owalkie_session_send_tx_eof_burst(owalkie_session_t* session) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->sendTxEofBurst());
}

owalkie_result owalkie_session_set_repeater_mode(owalkie_session_t* session, int enabled) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->setRepeaterMode(enabled != 0));
}

owalkie_result owalkie_session_reset_udp_transport(owalkie_session_t* session) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->resetUdpTransport());
}

void owalkie_session_get_state(
    const owalkie_session_t* session,
    owalkie_session_state* out_state) {
    const auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session || !out_state) {
        return;
    }
    const owalkie::SessionState st = wrapper->session->state();
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->connected = st.connected ? 1 : 0;
    out_state->udp_ready = st.udpReady ? 1 : 0;
    out_state->receiving = st.receiving ? 1 : 0;
    out_state->local_tx_active = st.localTxActive ? 1 : 0;
    out_state->ptt_server_locked = st.pttServerLocked ? 1 : 0;
    out_state->ptt_lock_display_sec = st.pttLockDisplaySec;
    out_state->has_welcome = st.hasWelcome ? 1 : 0;
    if (st.hasWelcome) {
        std::lock_guard<std::mutex> lg(wrapper->scratchMu);
        fillWelcomeC(st.welcome, out_state->config, wrapper->welcomeOpusScratch);
    }
}

owalkie::PowerProfile fromCPowerProfile(owalkie_power_profile profile) {
    switch (profile) {
        case OWALKIE_POWER_BACKGROUND:
            return owalkie::PowerProfile::Background;
        case OWALKIE_POWER_ACTIVE_TX:
            return owalkie::PowerProfile::ActiveTx;
        case OWALKIE_POWER_FOREGROUND:
        default:
            return owalkie::PowerProfile::Foreground;
    }
}

owalkie_power_profile toCPowerProfile(owalkie::PowerProfile profile) {
    switch (profile) {
        case owalkie::PowerProfile::Background:
            return OWALKIE_POWER_BACKGROUND;
        case owalkie::PowerProfile::ActiveTx:
            return OWALKIE_POWER_ACTIVE_TX;
        case owalkie::PowerProfile::Foreground:
        default:
            return OWALKIE_POWER_FOREGROUND;
    }
}

void owalkie_session_set_power_profile(owalkie_session_t* session, owalkie_power_profile profile) {
    auto* wrapper = asWrapper(session);
    if (wrapper && wrapper->session) {
        wrapper->session->setPowerProfile(fromCPowerProfile(profile));
    }
}

owalkie_power_profile owalkie_session_get_power_profile(const owalkie_session_t* session) {
    const auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return OWALKIE_POWER_FOREGROUND;
    }
    return toCPowerProfile(wrapper->session->powerProfile());
}

void owalkie_session_enter_udp_recovery(owalkie_session_t* session) {
    auto* wrapper = asWrapper(session);
    if (wrapper && wrapper->session) {
        wrapper->session->enterUdpRecovery();
    }
}

void owalkie_session_notify_network_changed(owalkie_session_t* session) {
    auto* wrapper = asWrapper(session);
    if (wrapper && wrapper->session) {
        wrapper->session->notifyNetworkChanged();
    }
}

owalkie_result owalkie_session_punch_udp_nat(owalkie_session_t* session) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->punchUdpNat());
}

owalkie_result owalkie_session_set_tx_signal_strength(
    owalkie_session_t* session,
    int strength_0_255) {
    auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(wrapper->session->setTxSignalStrength(strength_0_255));
}

int owalkie_session_get_tx_signal_strength(const owalkie_session_t* session) {
    const auto* wrapper = asWrapper(session);
    if (!wrapper || !wrapper->session) {
        return static_cast<int>(owalkie::pkt::kDefaultTxSignalStrength);
    }
    return wrapper->session->txSignalStrength();
}

#else

owalkie_result owalkie_session_create(owalkie_session_t**) { return OWALKIE_ERR_UNSUPPORTED; }
void owalkie_session_destroy(owalkie_session_t*) {}
void owalkie_session_set_callbacks(owalkie_session_t*, const owalkie_session_callbacks*) {}
owalkie_result owalkie_session_connect(owalkie_session_t*, const owalkie_connect_params*) {
    return OWALKIE_ERR_UNSUPPORTED;
}
void owalkie_session_disconnect(owalkie_session_t*) {}
int owalkie_session_is_connected(const owalkie_session_t*) { return 0; }
void owalkie_session_set_auto_reconnect(owalkie_session_t*, int) {}
int owalkie_session_auto_reconnect_enabled(const owalkie_session_t*) { return 0; }
owalkie_result owalkie_session_feed_tx_pcm(owalkie_session_t*, const int16_t*, size_t) {
    return OWALKIE_ERR_UNSUPPORTED;
}
owalkie_result owalkie_session_send_tx_opus(owalkie_session_t*, const uint8_t*, size_t, int) {
    return OWALKIE_ERR_UNSUPPORTED;
}
owalkie_result owalkie_session_send_tx_eof_burst(owalkie_session_t*) { return OWALKIE_ERR_UNSUPPORTED; }
owalkie_result owalkie_session_set_repeater_mode(owalkie_session_t*, int) {
    return OWALKIE_ERR_UNSUPPORTED;
}
owalkie_result owalkie_session_reset_udp_transport(owalkie_session_t*) {
    return OWALKIE_ERR_UNSUPPORTED;
}
void owalkie_session_get_state(const owalkie_session_t*, owalkie_session_state*) {}
void owalkie_session_set_power_profile(owalkie_session_t*, owalkie_power_profile) {}
owalkie_power_profile owalkie_session_get_power_profile(const owalkie_session_t*) {
    return OWALKIE_POWER_FOREGROUND;
}
void owalkie_session_enter_udp_recovery(owalkie_session_t*) {}
void owalkie_session_notify_network_changed(owalkie_session_t*) {}
owalkie_result owalkie_session_punch_udp_nat(owalkie_session_t*) {
    return OWALKIE_ERR_UNSUPPORTED;
}
owalkie_result owalkie_session_set_tx_signal_strength(owalkie_session_t*, int) {
    return OWALKIE_ERR_UNSUPPORTED;
}
int owalkie_session_get_tx_signal_strength(const owalkie_session_t*) {
    return static_cast<int>(owalkie::pkt::kDefaultTxSignalStrength);
}

#endif
