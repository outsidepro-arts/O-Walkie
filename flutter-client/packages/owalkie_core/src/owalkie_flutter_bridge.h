#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** O-Walkie core version string (e.g. "0.1.0"). */
FFI_PLUGIN_EXPORT const char* owalkie_flutter_core_version(void);

/** Negotiated protocol version constant from owalkie_core.h. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_protocol_version(void);

/** Returns 1 when owalkie-core was built with session transport. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_has_session(void);

typedef struct owalkie_flutter_polled_event {
    int32_t event_type;
    int64_t session_id;
    char info[512];
} owalkie_flutter_polled_event;

/** Release native relay/audio state (call before isolate shutdown). */
FFI_PLUGIN_EXPORT void owalkie_flutter_shutdown(void);

/**
 * Prepare managed session. Returns session id or 0 on failure.
 * TLS/wss is not supported (use_tls must be 0).
 */
FFI_PLUGIN_EXPORT int64_t owalkie_flutter_prepare(
    const char* host,
    int32_t port,
    const char* channel,
    int32_t repeater_mode);

/** Single connect attempt; call again after CONNECTION_LOST. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_connect(int64_t session_id, int32_t timeout_ms);

FFI_PLUGIN_EXPORT void owalkie_flutter_disconnect(int64_t session_id);
FFI_PLUGIN_EXPORT void owalkie_flutter_disconnect_all(void);

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_session_valid(int64_t session_id);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_session_ready(int64_t session_id);

/** RX playback gain 0..200 (%). */
FFI_PLUGIN_EXPORT void owalkie_flutter_set_rx_volume_percent(int32_t percent);

/** Begin local PTT (TX_OPEN + mic capture). */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_ptt_down(int64_t session_id);

/** End local PTT (TX_CLOSE + stop capture). */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_ptt_up(int64_t session_id);

/**
 * Poll session events posted from native worker threads.
 * @return 1 when @p out is filled, 0 when queue is empty.
 */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_poll_event(owalkie_flutter_polled_event* out);

/** Toggle repeater mode on an active session. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_set_repeater_mode(int64_t session_id, int32_t enabled);

/**
 * One-shot channel activity probe (no managed session).
 * @p out_active is 0 or 1 on success.
 */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_check_channel_activity(
    const char* host,
    int32_t port,
    const char* channel,
    int32_t timeout_ms,
    int32_t* out_active);

#ifdef __cplusplus
}
#endif
