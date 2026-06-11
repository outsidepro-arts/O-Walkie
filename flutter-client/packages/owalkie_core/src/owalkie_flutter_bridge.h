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
 * End PTT with optional roger uplink/local PCM (already generated at codec/local rate).
 * Pass null/0 count to skip roger (same as @c owalkie_flutter_ptt_up).
 */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_ptt_up_with_roger(
    int64_t session_id,
    const int16_t* roger_uplink,
    size_t roger_uplink_count,
    const int16_t* roger_local,
    size_t roger_local_count,
    int32_t local_sample_rate_hz);

/** Stream a pre-generated call pattern on the uplink; optional local preview. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_send_call(
    int64_t session_id,
    const int16_t* uplink_pcm,
    size_t uplink_count,
    const int16_t* local_pcm,
    size_t local_count,
    int32_t local_sample_rate_hz);

/** Codec sample rate / frame size from last OWALKIE_EV_CONNECTED welcome. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_codec_sample_rate(void);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_codec_frame_samples(void);

typedef struct owalkie_flutter_signal_spec {
    const double* freq_hz;
    const int32_t* duration_ms;
    size_t point_count;
    int32_t tail_ms;
    int32_t repeat_count;
    double gain;
} owalkie_flutter_signal_spec;

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_signal_generate(
    const owalkie_flutter_signal_spec* spec,
    int32_t sample_rate_hz,
    int16_t** out_samples,
    size_t* out_sample_count);

FFI_PLUGIN_EXPORT void owalkie_flutter_signal_free_pcm(int16_t* samples);

FFI_PLUGIN_EXPORT void owalkie_flutter_play_local_pcm(
    const int16_t* samples,
    size_t sample_count,
    int32_t sample_rate_hz);

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

/** Optional UDP NAT punch after network handoff (mode 0=wifi, 1=cell for report/clear). */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_punch_nat(int64_t session_id);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_recover_udp(int64_t session_id);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_report_signal(int32_t mode, int32_t value);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_clear_signal(int32_t mode);
/** Combined uplink signal byte 0..255 (255 = unknown / no samples). */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_get_uplink_signal_byte(void);
/** Android only: store network handle + bind process before connect (no-op elsewhere). */
FFI_PLUGIN_EXPORT void owalkie_flutter_bind_process_network(int64_t network_handle);

typedef struct owalkie_flutter_audio_device_info {
    int32_t index;
    int32_t is_default;
    char name[256];
} owalkie_flutter_audio_device_info;

/** List capture devices into @p out (max @p max_count). Returns count written. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_list_capture_devices(
    owalkie_flutter_audio_device_info* out,
    int32_t max_count);

/** List playback devices into @p out (max @p max_count). Returns count written. */
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_list_playback_devices(
    owalkie_flutter_audio_device_info* out,
    int32_t max_count);

FFI_PLUGIN_EXPORT void owalkie_flutter_set_capture_device_index(int32_t index);
FFI_PLUGIN_EXPORT void owalkie_flutter_set_playback_device_index(int32_t index);
FFI_PLUGIN_EXPORT void owalkie_flutter_set_capture_aaudio_input_preset(int32_t preset);
FFI_PLUGIN_EXPORT void owalkie_flutter_set_capture_platform_device_id(int32_t platform_id);
FFI_PLUGIN_EXPORT void owalkie_flutter_set_playback_platform_device_id(int32_t platform_id);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_get_capture_device_index(void);
FFI_PLUGIN_EXPORT int32_t owalkie_flutter_get_playback_device_index(void);

#ifdef __cplusplus
}
#endif
