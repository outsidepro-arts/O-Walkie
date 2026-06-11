#include "owalkie_flutter_bridge.h"
#include "owalkie_flutter_audio.h"

#include "owalkie_core.h"
#include "owalkie/session_manager.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <android/multinetwork.h>
#include <android/log.h>
#endif

#ifdef OWALKIE_CORE_HAS_SESSION

#if defined(__ANDROID__)
namespace {

std::atomic<long long> g_android_network_handle{0};
std::atomic<bool> g_android_pre_connect_hook_installed{false};

void ensure_android_pre_connect_hook() {
    bool expected = false;
    if (!g_android_pre_connect_hook_installed.compare_exchange_strong(expected, true)) {
        return;
    }
    owalkie::SessionManager::instance().setPreConnectHook([]() {
        const long long stored = g_android_network_handle.load(std::memory_order_relaxed);
        const net_handle_t handle =
            stored == 0LL ? NETWORK_UNSPECIFIED : static_cast<net_handle_t>(stored);
        const int rc = android_setprocnetwork(handle);
        __android_log_print(
            ANDROID_LOG_INFO,
            "OwalkieFlutter",
            "preConnect android_setprocnetwork handle=%lld rc=%d",
            static_cast<long long>(stored),
            rc);
    });
}

} // namespace
#endif

namespace {

std::mutex g_event_mu;
std::deque<owalkie_flutter_polled_event> g_events;
constexpr size_t kMaxEvents = 256;

std::atomic<owalkie_session_id> g_active_session{0};
std::atomic<bool> g_ptt_active{false};
std::atomic<int> g_codec_sample_rate{8000};
std::atomic<int> g_codec_frame_samples{160};

void stream_pcm_frames(
    owalkie_session_id sid,
    const int16_t* pcm,
    size_t sample_count,
    int frame_samples) {
    if (!pcm || sample_count == 0 || frame_samples <= 0) {
        return;
    }
    const size_t frame_sz = static_cast<size_t>(frame_samples);
    std::vector<int16_t> frame(frame_sz, 0);
    size_t offset = 0;
    while (offset < sample_count) {
        const size_t chunk = std::min(frame_sz, sample_count - offset);
        std::fill(frame.begin(), frame.end(), 0);
        std::memcpy(frame.data(), pcm + offset, chunk * sizeof(int16_t));
        (void)owalkie_tx_submit(sid, OWALKIE_TX_PCM, frame.data(), frame.size(), nullptr, 0);
        offset += chunk;
    }
}

void push_event(owalkie_event_type type, owalkie_session_id sid, const char* info) {
    owalkie_flutter_polled_event ev{};
    ev.event_type = static_cast<int32_t>(type);
    ev.session_id = static_cast<int64_t>(sid);
    if (info) {
        std::strncpy(ev.info, info, sizeof(ev.info) - 1);
    }
    std::lock_guard<std::mutex> lock(g_event_mu);
    if (g_events.size() >= kMaxEvents) {
        g_events.pop_front();
    }
    g_events.push_back(ev);
}

void format_event_info(const owalkie_event* ev, char* buf, size_t bufsize) {
    if (!ev || !buf || bufsize == 0) {
        return;
    }
    buf[0] = '\0';
    switch (ev->type) {
        case OWALKIE_EV_CONNECTION_FAILED:
        case OWALKIE_EV_DISCONNECTED:
        case OWALKIE_EV_CONNECTION_LOST:
            if (ev->u.disconnected.reason) {
                std::strncpy(buf, ev->u.disconnected.reason, bufsize - 1);
            }
            break;
        case OWALKIE_EV_PROTOCOL_ERROR:
            if (ev->u.protocol_error.message) {
                std::strncpy(buf, ev->u.protocol_error.message, bufsize - 1);
            }
            break;
        case OWALKIE_EV_RX_BROADCAST_START:
            std::snprintf(buf, bufsize, "%s", ev->u.rx_broadcast_start.busy_mode ? "true" : "false");
            break;
        case OWALKIE_EV_PTT_LOCKED:
        case OWALKIE_EV_TX_COUNTDOWN_START:
            std::snprintf(buf, bufsize, "%d", ev->u.ptt_locked.display_sec);
            break;
        case OWALKIE_EV_TX_STOP:
            if (ev->u.tx_stop.info) {
                std::strncpy(buf, ev->u.tx_stop.info, bufsize - 1);
            }
            break;
        default:
            break;
    }
}

void on_session_event(void* /*user*/, owalkie_session_id sid, const owalkie_event* ev) {
    if (!ev) {
        return;
    }
    char info_buf[512]{};
    format_event_info(ev, info_buf, sizeof(info_buf));
    push_event(ev->type, sid, info_buf[0] != '\0' ? info_buf : nullptr);
    if (ev->type == OWALKIE_EV_CONNECTED) {
        const auto& cfg = ev->u.welcome.config;
        owalkie_flutter_audio::configure(cfg.sample_rate, cfg.packet_ms);
        g_codec_sample_rate.store(cfg.sample_rate);
        g_codec_frame_samples.store(owalkie_frame_samples(cfg.sample_rate, cfg.packet_ms));
        owalkie_set_power_profile(sid, OWALKIE_POWER_FOREGROUND);
    }
    if (ev->type == OWALKIE_EV_DISCONNECTED ||
        ev->type == OWALKIE_EV_CONNECTION_FAILED ||
        ev->type == OWALKIE_EV_PROTOCOL_ERROR) {
        g_ptt_active.store(false, std::memory_order_release);
        owalkie_flutter_audio::stop_capture();
        if (g_active_session.load(std::memory_order_acquire) == sid) {
            g_active_session.store(0, std::memory_order_release);
        }
    }
}

void on_rx_pcm(
    void* /*user*/,
    owalkie_session_id /*sid*/,
    const int16_t* samples,
    size_t sample_count,
    int sample_rate,
    int /*packet_ms*/) {
    owalkie_flutter_audio::on_rx_pcm(samples, sample_count, sample_rate);
}

void on_tx_frame(const int16_t* frame, size_t sample_count, void* user) {
    const auto sid = static_cast<owalkie_session_id>(reinterpret_cast<intptr_t>(user));
    if (sid == 0 || sample_count == 0) {
        return;
    }
    (void)owalkie_tx_submit(sid, OWALKIE_TX_PCM, frame, sample_count, nullptr, 0);
}

} // namespace

#endif // OWALKIE_CORE_HAS_SESSION

extern "C" {

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_has_session(void) {
#ifdef OWALKIE_CORE_HAS_SESSION
    return 1;
#else
    return 0;
#endif
}

FFI_PLUGIN_EXPORT void owalkie_flutter_shutdown(void) {
    owalkie_flutter_audio::shutdown();
#ifdef OWALKIE_CORE_HAS_SESSION
    owalkie_flutter_audio::set_tx_frame_callback(nullptr, nullptr);
    g_ptt_active.store(false, std::memory_order_release);
    g_active_session.store(0, std::memory_order_release);
    owalkie_disconnect_all();
    {
        std::lock_guard<std::mutex> lock(g_event_mu);
        g_events.clear();
    }
#endif
}

FFI_PLUGIN_EXPORT int64_t owalkie_flutter_prepare(
    const char* host,
    int32_t port,
    const char* channel,
    int32_t repeater_mode) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (!host || !channel || port <= 0) {
        return 0;
    }
    owalkie_managed_callbacks cbs{};
    cbs.on_session_event = on_session_event;
    cbs.on_rx_pcm = on_rx_pcm;
    cbs.user_data = nullptr;
    owalkie_connect_params params{};
    params.host = host;
    params.port = port;
    params.channel = channel;
    params.use_tls = 0;
    params.repeater_mode = repeater_mode ? 1 : 0;
    const owalkie_session_id sid = owalkie_prepare_connection(&params, &cbs);
    if (sid != owalkie_invalid_session_id()) {
        g_active_session.store(sid, std::memory_order_release);
        owalkie_flutter_audio::set_tx_frame_callback(
            on_tx_frame,
            reinterpret_cast<void*>(static_cast<intptr_t>(sid)));
    }
    return static_cast<int64_t>(sid);
#else
    (void)host;
    (void)port;
    (void)channel;
    (void)repeater_mode;
    return 0;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_connect(int64_t session_id, int32_t timeout_ms) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id <= 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return static_cast<int32_t>(
        owalkie_connect(static_cast<owalkie_session_id>(session_id), timeout_ms));
#else
    (void)session_id;
    (void)timeout_ms;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT void owalkie_flutter_disconnect(int64_t session_id) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id <= 0) {
        return;
    }
    g_ptt_active.store(false, std::memory_order_release);
    owalkie_flutter_audio::stop_capture();
    owalkie_disconnect(static_cast<owalkie_session_id>(session_id));
    if (g_active_session.load(std::memory_order_acquire) ==
        static_cast<owalkie_session_id>(session_id)) {
        g_active_session.store(0, std::memory_order_release);
    }
#else
    (void)session_id;
#endif
}

FFI_PLUGIN_EXPORT void owalkie_flutter_disconnect_all(void) {
#ifdef OWALKIE_CORE_HAS_SESSION
    g_ptt_active.store(false, std::memory_order_release);
    owalkie_flutter_audio::stop_capture();
    owalkie_disconnect_all();
    g_active_session.store(0, std::memory_order_release);
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_session_valid(int64_t session_id) {
#ifdef OWALKIE_CORE_HAS_SESSION
    return owalkie_session_id_valid(static_cast<owalkie_session_id>(session_id)) ? 1 : 0;
#else
    (void)session_id;
    return 0;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_session_ready(int64_t session_id) {
#ifdef OWALKIE_CORE_HAS_SESSION
    return owalkie_session_id_ready(static_cast<owalkie_session_id>(session_id)) ? 1 : 0;
#else
    (void)session_id;
    return 0;
#endif
}

FFI_PLUGIN_EXPORT void owalkie_flutter_set_rx_volume_percent(int32_t percent) {
    owalkie_flutter_audio::set_rx_volume_percent(percent);
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_ptt_down(int64_t session_id) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id <= 0 || !owalkie_session_id_ready(static_cast<owalkie_session_id>(session_id))) {
        return OWALKIE_ERR_NOT_READY;
    }
    if (g_ptt_active.exchange(true)) {
        return OWALKIE_OK;
    }
    owalkie_set_power_profile(static_cast<owalkie_session_id>(session_id), OWALKIE_POWER_ACTIVE_TX);
    const owalkie_result open_res =
        owalkie_tx_submit(static_cast<owalkie_session_id>(session_id), OWALKIE_TX_OPEN, nullptr, 0, nullptr, 0);
    if (open_res != OWALKIE_OK) {
        g_ptt_active.store(false, std::memory_order_release);
        return static_cast<int32_t>(open_res);
    }
    if (!owalkie_flutter_audio::start_capture()) {
        (void)owalkie_tx_submit(
            static_cast<owalkie_session_id>(session_id), OWALKIE_TX_ABORT, nullptr, 0, nullptr, 0);
        g_ptt_active.store(false, std::memory_order_release);
        return OWALKIE_ERR_INTERNAL;
    }
    return OWALKIE_OK;
#else
    (void)session_id;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_ptt_up(int64_t session_id) {
    return owalkie_flutter_ptt_up_with_roger(session_id, nullptr, 0, nullptr, 0, 0);
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_ptt_up_with_roger(
    int64_t session_id,
    const int16_t* roger_uplink,
    size_t roger_uplink_count,
    const int16_t* roger_local,
    size_t roger_local_count,
    int32_t local_sample_rate_hz) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (!g_ptt_active.exchange(false)) {
        return OWALKIE_OK;
    }
    owalkie_flutter_audio::stop_capture();
    if (session_id <= 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const auto sid = static_cast<owalkie_session_id>(session_id);
    const bool has_roger = roger_uplink != nullptr && roger_uplink_count > 0;
    std::thread local_thread;
    if (roger_local != nullptr && roger_local_count > 0 && local_sample_rate_hz > 0) {
        local_thread = std::thread([roger_local, roger_local_count, local_sample_rate_hz]() {
            owalkie_flutter_audio::play_local_pcm_blocking(
                roger_local, roger_local_count, local_sample_rate_hz);
        });
    }
    if (has_roger) {
        (void)owalkie_tx_submit(sid, OWALKIE_TX_VOICE_END, nullptr, 0, nullptr, 0);
        (void)owalkie_tx_wait_idle(sid, 500);
        stream_pcm_frames(
            sid, roger_uplink, roger_uplink_count, g_codec_frame_samples.load());
        (void)owalkie_tx_wait_idle(sid, 500);
    }
    (void)owalkie_tx_submit(sid, OWALKIE_TX_CLOSE, nullptr, 0, nullptr, 0);
    (void)owalkie_tx_wait_idle(sid, 500);
    owalkie_set_power_profile(sid, OWALKIE_POWER_FOREGROUND);
    if (local_thread.joinable()) {
        local_thread.join();
    }
    return OWALKIE_OK;
#else
    (void)session_id;
    (void)roger_uplink;
    (void)roger_uplink_count;
    (void)roger_local;
    (void)roger_local_count;
    (void)local_sample_rate_hz;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_send_call(
    int64_t session_id,
    const int16_t* uplink_pcm,
    size_t uplink_count,
    const int16_t* local_pcm,
    size_t local_count,
    int32_t local_sample_rate_hz) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id <= 0 || !uplink_pcm || uplink_count == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    if (!owalkie_session_id_ready(static_cast<owalkie_session_id>(session_id))) {
        return OWALKIE_ERR_NOT_READY;
    }
    const auto sid = static_cast<owalkie_session_id>(session_id);
    std::thread local_thread;
    if (local_pcm != nullptr && local_count > 0 && local_sample_rate_hz > 0) {
        local_thread = std::thread([local_pcm, local_count, local_sample_rate_hz]() {
            owalkie_flutter_audio::play_local_pcm_blocking(
                local_pcm, local_count, local_sample_rate_hz);
        });
    }
    owalkie_set_power_profile(sid, OWALKIE_POWER_ACTIVE_TX);
    const owalkie_result open_res =
        owalkie_tx_submit(sid, OWALKIE_TX_OPEN, nullptr, 0, nullptr, 0);
    if (open_res != OWALKIE_OK) {
        owalkie_set_power_profile(sid, OWALKIE_POWER_FOREGROUND);
        if (local_thread.joinable()) {
            local_thread.join();
        }
        return static_cast<int32_t>(open_res);
    }
    stream_pcm_frames(sid, uplink_pcm, uplink_count, g_codec_frame_samples.load());
    (void)owalkie_tx_submit(sid, OWALKIE_TX_CLOSE, nullptr, 0, nullptr, 0);
    (void)owalkie_tx_wait_idle(sid, 500);
    owalkie_set_power_profile(sid, OWALKIE_POWER_FOREGROUND);
    if (local_thread.joinable()) {
        local_thread.join();
    }
    return OWALKIE_OK;
#else
    (void)session_id;
    (void)uplink_pcm;
    (void)uplink_count;
    (void)local_pcm;
    (void)local_count;
    (void)local_sample_rate_hz;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_codec_sample_rate(void) {
#ifdef OWALKIE_CORE_HAS_SESSION
    return g_codec_sample_rate.load();
#else
    return 8000;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_codec_frame_samples(void) {
#ifdef OWALKIE_CORE_HAS_SESSION
    return g_codec_frame_samples.load();
#else
    return 160;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_signal_generate(
    const owalkie_flutter_signal_spec* spec,
    int32_t sample_rate_hz,
    int16_t** out_samples,
    size_t* out_sample_count) {
    if (!spec || !out_samples || !out_sample_count || sample_rate_hz <= 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    if (spec->point_count == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    if (!spec->freq_hz || !spec->duration_ms) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    std::vector<owalkie_signal_point> pts(spec->point_count);
    for (size_t i = 0; i < spec->point_count; ++i) {
        pts[i].freq_hz = spec->freq_hz[i];
        pts[i].duration_ms = spec->duration_ms[i];
    }
    owalkie_signal_pattern pattern{};
    pattern.points = pts.data();
    pattern.point_count = pts.size();
    pattern.tail_ms = spec->tail_ms;
    pattern.repeat_count = spec->repeat_count;
    pattern.gain = spec->gain;
    return static_cast<int32_t>(
        owalkie_signal_generate_pcm(&pattern, sample_rate_hz, out_samples, out_sample_count));
}

FFI_PLUGIN_EXPORT void owalkie_flutter_signal_free_pcm(int16_t* samples) {
    owalkie_signal_free_pcm(samples);
}

FFI_PLUGIN_EXPORT void owalkie_flutter_play_local_pcm(
    const int16_t* samples,
    size_t sample_count,
    int32_t sample_rate_hz) {
    owalkie_flutter_audio::play_local_pcm_blocking(samples, sample_count, sample_rate_hz);
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_poll_event(owalkie_flutter_polled_event* out) {
    if (!out) {
        return 0;
    }
#ifdef OWALKIE_CORE_HAS_SESSION
    std::lock_guard<std::mutex> lock(g_event_mu);
    if (g_events.empty()) {
        return 0;
    }
    *out = g_events.front();
    g_events.pop_front();
    return 1;
#else
    return 0;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_set_repeater_mode(int64_t session_id, int32_t enabled) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id <= 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return static_cast<int32_t>(owalkie_set_repeater_mode(
        static_cast<owalkie_session_id>(session_id),
        enabled ? 1 : 0));
#else
    (void)session_id;
    (void)enabled;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_check_channel_activity(
    const char* host,
    int32_t port,
    const char* channel,
    int32_t timeout_ms,
    int32_t* out_active) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (!host || !channel || port <= 0 || !out_active) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    owalkie_connect_params params{};
    params.host = host;
    params.port = port;
    params.channel = channel;
    params.use_tls = 0;
    params.repeater_mode = 0;
    int active = 0;
    const owalkie_result r =
        owalkie_check_channel_activity(&params, timeout_ms, &active);
    *out_active = active;
    return static_cast<int32_t>(r);
#else
    (void)host;
    (void)port;
    (void)channel;
    (void)timeout_ms;
    if (out_active) {
        *out_active = 0;
    }
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_punch_nat(int64_t session_id) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return static_cast<int32_t>(
        owalkie_punch_nat(static_cast<owalkie_session_id>(session_id)));
#else
    (void)session_id;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_recover_udp(int64_t session_id) {
#ifdef OWALKIE_CORE_HAS_SESSION
    if (session_id == 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    return static_cast<int32_t>(
        owalkie_recover_udp_transport(static_cast<owalkie_session_id>(session_id)));
#else
    (void)session_id;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT void owalkie_flutter_bind_process_network(int64_t network_handle) {
#if defined(__ANDROID__)
    g_android_network_handle.store(network_handle, std::memory_order_relaxed);
    ensure_android_pre_connect_hook();
    const net_handle_t handle = network_handle == 0LL
        ? NETWORK_UNSPECIFIED
        : static_cast<net_handle_t>(network_handle);
    const int rc = android_setprocnetwork(handle);
    __android_log_print(
        ANDROID_LOG_INFO,
        "OwalkieFlutter",
        "bindProcessNetwork handle=%lld rc=%d",
        network_handle,
        rc);
#else
    (void)network_handle;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_report_signal(int32_t mode, int32_t value) {
#ifdef OWALKIE_CORE_HAS_SESSION
    const owalkie_signal_mode signal_mode =
        mode == 1 ? OWALKIE_SIGNAL_CELL : OWALKIE_SIGNAL_WIFI;
    return static_cast<int32_t>(owalkie_report_signal(signal_mode, value));
#else
    (void)mode;
    (void)value;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_clear_signal(int32_t mode) {
#ifdef OWALKIE_CORE_HAS_SESSION
    const owalkie_signal_mode signal_mode =
        mode == 1 ? OWALKIE_SIGNAL_CELL : OWALKIE_SIGNAL_WIFI;
    return static_cast<int32_t>(owalkie_clear_signal(signal_mode));
#else
    (void)mode;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_get_uplink_signal_byte(void) {
#ifdef OWALKIE_CORE_HAS_SESSION
    return owalkie_get_uplink_signal_byte();
#else
    return 255;
#endif
}

} // extern "C"
