#include "owalkie_flutter_bridge.h"
#include "owalkie_flutter_audio.h"

#include "owalkie_core.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#ifdef OWALKIE_CORE_HAS_SESSION

namespace {

std::mutex g_event_mu;
std::deque<owalkie_flutter_polled_event> g_events;
constexpr size_t kMaxEvents = 256;

std::atomic<owalkie_session_id> g_active_session{0};
std::atomic<bool> g_ptt_active{false};

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

const char* event_info(const owalkie_event* ev) {
    if (!ev) {
        return nullptr;
    }
    switch (ev->type) {
        case OWALKIE_EV_CONNECTION_FAILED:
        case OWALKIE_EV_DISCONNECTED:
        case OWALKIE_EV_CONNECTION_LOST:
            return ev->u.disconnected.reason;
        case OWALKIE_EV_PROTOCOL_ERROR:
            return ev->u.protocol_error.message;
        case OWALKIE_EV_RX_BROADCAST_START:
            return ev->u.rx_broadcast_start.busy_mode ? "busy" : "idle";
        case OWALKIE_EV_PTT_LOCKED:
            return "locked";
        case OWALKIE_EV_TX_STOP:
            return ev->u.tx_stop.info;
        default:
            return nullptr;
    }
}

void on_session_event(void* /*user*/, owalkie_session_id sid, const owalkie_event* ev) {
    if (!ev) {
        return;
    }
    push_event(ev->type, sid, event_info(ev));
    if (ev->type == OWALKIE_EV_CONNECTED) {
        const auto& cfg = ev->u.welcome.config;
        owalkie_flutter_audio::configure(cfg.sample_rate, cfg.packet_ms);
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
#ifdef OWALKIE_CORE_HAS_SESSION
    if (!g_ptt_active.exchange(false)) {
        return OWALKIE_OK;
    }
    owalkie_flutter_audio::stop_capture();
    if (session_id <= 0) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    const auto sid = static_cast<owalkie_session_id>(session_id);
    (void)owalkie_tx_submit(sid, OWALKIE_TX_CLOSE, nullptr, 0, nullptr, 0);
    (void)owalkie_tx_wait_idle(sid, 500);
    owalkie_set_power_profile(sid, OWALKIE_POWER_FOREGROUND);
    return OWALKIE_OK;
#else
    (void)session_id;
    return OWALKIE_ERR_UNSUPPORTED;
#endif
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

} // extern "C"
