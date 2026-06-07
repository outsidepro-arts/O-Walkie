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
#include "owalkie/link_signal.hpp"
#include "owalkie/protocol.hpp"
#include "owalkie/session.hpp"
#include "owalkie/client_events.hpp"
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

void fillSessionInfoC(
    bool ready,
    const owalkie::SessionState& st,
    owalkie_session_info& out,
    std::string& opusAppScratch) {
    std::memset(&out, 0, sizeof(out));
    out.ready = ready ? 1 : 0;
    out.connected = st.connected ? 1 : 0;
    out.udp_ready = st.udpReady ? 1 : 0;
    out.connection_lost = st.connectionLost ? 1 : 0;
    out.receiving = st.receiving ? 1 : 0;
    out.local_tx_active = st.localTxActive ? 1 : 0;
    out.ptt_server_locked = st.pttServerLocked ? 1 : 0;
    out.ptt_lock_display_sec = st.pttLockDisplaySec;
    if (st.hasWelcome) {
        out.has_config = 1;
        fillWelcomeC(st.welcome, out.config, opusAppScratch);
    }
}

void fillManagedEventC(
    const owalkie::Event& ev,
    owalkie_event& cev,
    std::string& scratch) {
    std::memset(&cev, 0, sizeof(cev));
    cev.type = owalkie::client_events::toPublic(ev.type);
    switch (ev.type) {
        case owalkie::EventType::Connected:
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
        case owalkie::EventType::ConnectionFailed:
        case owalkie::EventType::Disconnected:
        case owalkie::EventType::ConnectionLost:
            cev.u.disconnected.code = ev.disconnectCode;
            scratch = ev.disconnectReason;
            cev.u.disconnected.reason = scratch.empty() ? nullptr : scratch.c_str();
            break;
        default:
            break;
    }
}

thread_local std::vector<uint8_t> g_udpUnpackScratch;

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
    if (ev.type == owalkie::EventType::Welcome) {
        out_event->type = OWALKIE_EV_CONNECTED;
    } else if (!owalkie::client_events::isVisible(ev.type)) {
        return OWALKIE_OK;
    } else {
        out_event->type = owalkie::client_events::toPublic(ev.type);
    }
    switch (ev.type) {
        case owalkie::EventType::Welcome:
        case owalkie::EventType::Connected: {
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
    sessionCbs.onRxPcm = [bridge](std::span<const int16_t> pcm, int sampleRate, int packetMs) {
        if (!bridge->cbs.on_rx_pcm || pcm.empty()) {
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
        bridge->cbs.on_rx_pcm(
            bridge->cbs.user_data,
            sid,
            pcm.data(),
            pcm.size(),
            sampleRate,
            packetMs);
    };
    sessionCbs.onSessionEvent = [bridge](const owalkie::Event& ev) {
        if (!bridge->cbs.on_session_event || !owalkie::client_events::isVisible(ev.type)) {
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
        if (ev.type == owalkie::EventType::ConnectionFailed ||
            ev.type == owalkie::EventType::Disconnected ||
            ev.type == owalkie::EventType::ProtocolError) {
            std::lock_guard<std::mutex> lock(g_managedBridgeMu);
            g_managedBridges.erase(sid);
        }
    };
    return sessionCbs;
}

} // namespace

owalkie_session_id owalkie_prepare_connection(
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

    const owalkie::SessionId id = owalkie::SessionManager::instance().prepareConnection(
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

owalkie_result owalkie_connect(owalkie_session_id session_id, int timeout_ms) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const owalkie::Result r =
        owalkie::SessionManager::instance().connect(session_id, timeout_ms);
    if (r == owalkie::Result::Ok &&
        owalkie::SessionManager::instance().isSessionReady(session_id)) {
        return OWALKIE_OK;
    }
    return toC(r);
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

owalkie_result owalkie_get_session_info(
    owalkie_session_id session_id,
    owalkie_session_info* out_info,
    char* opus_application_buf,
    size_t opus_application_buf_size) {
    if (session_id == owalkie_invalid_session_id() || !out_info) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie::SessionState st{};
    bool ready = false;
    const owalkie::Result r =
        owalkie::SessionManager::instance().getSessionInfo(session_id, &st, &ready);
    if (r != owalkie::Result::Ok) {
        return toC(r);
    }
    std::string opusScratch;
    fillSessionInfoC(ready, st, *out_info, opusScratch);
    if (out_info->has_config && opus_application_buf && opus_application_buf_size > 0) {
        if (!copyToBuf(st.welcome.opus.application, opus_application_buf, opus_application_buf_size)) {
            return OWALKIE_ERR_BUFFER_TOO_SMALL;
        }
        out_info->config.opus_application = opus_application_buf;
    }
    return OWALKIE_OK;
}

owalkie_result owalkie_tx_start(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().txStart(session_id));
}

owalkie_result owalkie_push_tx_pcm(
    owalkie_session_id session_id,
    const int16_t* samples,
    size_t sample_count) {
    if (session_id == owalkie_invalid_session_id() || !samples || sample_count == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().pushTxPcm(
        session_id,
        std::span<const int16_t>(samples, sample_count)));
}

owalkie_result owalkie_tx_end(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().txEnd(session_id));
}

owalkie_result owalkie_set_repeater_mode(owalkie_session_id session_id, int enabled) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().setRepeaterMode(session_id, enabled != 0));
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

owalkie_result owalkie_punch_nat(owalkie_session_id session_id) {
    if (session_id == owalkie_invalid_session_id()) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::SessionManager::instance().punchNat(session_id));
}

owalkie_result owalkie_report_signal(owalkie_signal_mode mode, int value) {
    owalkie::link_signal::Mode cpp = owalkie::link_signal::Mode::Wifi;
    switch (mode) {
        case OWALKIE_SIGNAL_WIFI:
            cpp = owalkie::link_signal::Mode::Wifi;
            break;
        case OWALKIE_SIGNAL_CELL:
            cpp = owalkie::link_signal::Mode::Cell;
            break;
        default:
            return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::link_signal::Registry::instance().report(cpp, value));
}

owalkie_result owalkie_clear_signal(owalkie_signal_mode mode) {
    owalkie::link_signal::Mode cpp = owalkie::link_signal::Mode::Wifi;
    switch (mode) {
        case OWALKIE_SIGNAL_WIFI:
            cpp = owalkie::link_signal::Mode::Wifi;
            break;
        case OWALKIE_SIGNAL_CELL:
            cpp = owalkie::link_signal::Mode::Cell;
            break;
        default:
            return OWALKIE_ERR_INVALID_ARG;
    }
    return toC(owalkie::link_signal::Registry::instance().clear(cpp));
}

int owalkie_get_uplink_signal_byte(void) {
    return owalkie::link_signal::Registry::instance().combinedByte();
}

#else

owalkie_session_id owalkie_prepare_connection(
    const owalkie_connect_params*,
    const owalkie_managed_callbacks*) {
    return owalkie_invalid_session_id();
}
owalkie_result owalkie_connect(owalkie_session_id, int) { return OWALKIE_ERR_UNSUPPORTED; }
void owalkie_disconnect(owalkie_session_id) {}
void owalkie_disconnect_all(void) {}
void owalkie_disconnect_all_and_wait(int) {}
int owalkie_session_id_valid(owalkie_session_id) { return 0; }
int owalkie_session_id_ready(owalkie_session_id) { return 0; }
owalkie_result owalkie_get_session_info(
    owalkie_session_id,
    owalkie_session_info*,
    char*,
    size_t) {
    return OWALKIE_ERR_UNSUPPORTED;
}
owalkie_result owalkie_tx_start(owalkie_session_id) { return OWALKIE_ERR_UNSUPPORTED; }
owalkie_result owalkie_push_tx_pcm(owalkie_session_id, const int16_t*, size_t) {
    return OWALKIE_ERR_UNSUPPORTED;
}
owalkie_result owalkie_tx_end(owalkie_session_id) { return OWALKIE_ERR_UNSUPPORTED; }
owalkie_result owalkie_set_repeater_mode(owalkie_session_id, int) {
    return OWALKIE_ERR_UNSUPPORTED;
}
void owalkie_set_power_profile(owalkie_session_id, owalkie_power_profile) {}
owalkie_result owalkie_punch_nat(owalkie_session_id) { return OWALKIE_ERR_UNSUPPORTED; }
owalkie_result owalkie_report_signal(owalkie_signal_mode, int) { return OWALKIE_ERR_UNSUPPORTED; }
owalkie_result owalkie_clear_signal(owalkie_signal_mode) { return OWALKIE_ERR_UNSUPPORTED; }
int owalkie_get_uplink_signal_byte(void) { return 255; }

#endif
