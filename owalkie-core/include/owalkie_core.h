#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define OWALKIE_PROTOCOL_VERSION 2

typedef enum owalkie_result {
    OWALKIE_OK = 0,
    OWALKIE_ERR_INVALID_ARG = 1,
    OWALKIE_ERR_ALREADY_CONNECTED = 2,
    OWALKIE_ERR_NOT_CONNECTED = 3,
    OWALKIE_ERR_PROTOCOL = 4,
    OWALKIE_ERR_NETWORK = 5,
    OWALKIE_ERR_INTERNAL = 6,
    OWALKIE_ERR_UNSUPPORTED = 7,
    OWALKIE_ERR_BUFFER_TOO_SMALL = 8,
    OWALKIE_ERR_NOT_READY = 9,
    OWALKIE_ERR_QUEUE_FULL = 10,
} owalkie_result;

typedef enum owalkie_tx_op {
    OWALKIE_TX_OPEN = 0,
    OWALKIE_TX_PCM = 1,
    OWALKIE_TX_OPUS = 2,
    OWALKIE_TX_VOICE_END = 3,
    OWALKIE_TX_CLOSE = 4,
    OWALKIE_TX_ABORT = 5,
} owalkie_tx_op;

typedef uint64_t owalkie_session_id;
inline owalkie_session_id owalkie_invalid_session_id(void) { return 0; }

/** Client-visible session events (internal transport / welcome steps are not emitted). */
typedef enum owalkie_event_type {
    OWALKIE_EV_CONNECTED = 0,
    OWALKIE_EV_DISCONNECTED,
    OWALKIE_EV_PROTOCOL_ERROR,
    OWALKIE_EV_CONNECTION_FAILED,
    OWALKIE_EV_RX_BROADCAST_START,
    OWALKIE_EV_RX_BROADCAST_END,
    OWALKIE_EV_PTT_LOCKED,
    OWALKIE_EV_PTT_UNLOCKED,
    OWALKIE_EV_TX_COUNTDOWN_START,
    OWALKIE_EV_TX_STOP,
    /** Transport lost; client should call @c owalkie_connect(session_id) until @c OWALKIE_EV_CONNECTED. */
    OWALKIE_EV_CONNECTION_LOST,
} owalkie_event_type;

typedef struct owalkie_welcome_config {
    uint32_t session_id;
    int protocol_version;
    int sample_rate;
    int packet_ms;
    int opus_bitrate;
    int opus_complexity;
    int opus_fec;
    int opus_dtx;
    const char* opus_application;
    int busy_mode;
    int transmit_timeout_sec;
} owalkie_welcome_config;

typedef struct owalkie_event {
    owalkie_event_type type;
    union {
        struct {
            owalkie_welcome_config config;
        } welcome;
        struct {
            int busy_mode;
        } rx_broadcast_start;
        struct {
            int end_delay_ms;
        } rx_broadcast_end;
        struct {
            int display_sec;
        } ptt_locked;
        struct {
            const char* info;
        } tx_stop;
        struct {
            const char* message;
        } protocol_error;
        struct {
            int code;
            const char* reason;
        } disconnected;
    } u;
} owalkie_event;

/** Thread-safe snapshot of a managed session (welcome/transport/runtime flags). */
typedef struct owalkie_session_info {
    int ready;
    int connected;
    int udp_ready;
    int connection_lost;
    int receiving;
    int local_tx_active;
    int ptt_server_locked;
    int ptt_lock_display_sec;
    int has_config;
    owalkie_welcome_config config;
} owalkie_session_info;

typedef struct owalkie_connect_params {
    const char* host;
    int port;
    const char* channel;
    int use_tls;
    int repeater_mode;
} owalkie_connect_params;

typedef void (*owalkie_on_rx_pcm_fn)(
    void* user_data,
    const int16_t* samples,
    size_t sample_count,
    int sample_rate,
    int packet_ms);

typedef void (*owalkie_managed_event_fn)(
    void* user_data,
    owalkie_session_id session_id,
    const owalkie_event* event);

typedef void (*owalkie_managed_rx_pcm_fn)(
    void* user_data,
    owalkie_session_id session_id,
    const int16_t* samples,
    size_t sample_count,
    int sample_rate,
    int packet_ms);

typedef struct owalkie_managed_callbacks {
    owalkie_managed_event_fn on_session_event;
    owalkie_managed_rx_pcm_fn on_rx_pcm;
    void* user_data;
} owalkie_managed_callbacks;

typedef struct owalkie_signal_point {
    double freq_hz;
    int duration_ms;
} owalkie_signal_point;

typedef struct owalkie_signal_pattern {
    const owalkie_signal_point* points;
    size_t point_count;
    int tail_ms;
    int repeat_count;
    double gain;
} owalkie_signal_pattern;

typedef struct owalkie_udp_audio_packet {
    uint32_t session_id;
    uint32_t sequence;
    uint8_t signal_strength;
    const uint8_t* opus;
    size_t opus_len;
} owalkie_udp_audio_packet;

/* --- version --- */
const char* owalkie_version_string(void);
int owalkie_version_major(void);
int owalkie_version_minor(void);

/* --- protocol normalize --- */
int owalkie_normalize_sample_rate(int value);
int owalkie_normalize_packet_ms(int value);
int owalkie_frame_samples(int sample_rate, int packet_ms);

/* --- JSON utilities --- */
owalkie_result owalkie_json_parse_welcome(
    const char* json_text,
    size_t json_len,
    owalkie_welcome_config* out_config,
    char* opus_application_buf,
    size_t opus_application_buf_size);

owalkie_result owalkie_json_parse_server_message(
    const char* json_text,
    size_t json_len,
    owalkie_event* out_event,
    char* string_buf,
    size_t string_buf_size);

/* --- signal utilities --- */
owalkie_result owalkie_signal_generate_pcm(
    const owalkie_signal_pattern* pattern,
    int sample_rate_hz,
    int16_t** out_samples,
    size_t* out_sample_count);

void owalkie_signal_free_pcm(int16_t* samples);

/* --- UDP utilities --- */
owalkie_result owalkie_udp_pack(
    const owalkie_udp_audio_packet* packet,
    uint8_t* out_buf,
    size_t out_buf_size,
    size_t* out_len);

owalkie_result owalkie_udp_unpack(
    const uint8_t* buf,
    size_t buf_len,
    owalkie_udp_audio_packet* out_packet);

int owalkie_udp_is_keepalive_signal(const uint8_t* buf, size_t len);
int owalkie_udp_is_keepalive_ack(const uint8_t* buf, size_t len, uint32_t session_id);
int owalkie_udp_is_tx_eof_marker(const uint8_t* buf, size_t len);

typedef enum owalkie_power_profile {
    OWALKIE_POWER_FOREGROUND = 0,
    OWALKIE_POWER_BACKGROUND,
    OWALKIE_POWER_ACTIVE_TX,
} owalkie_power_profile;

typedef enum owalkie_signal_mode {
    OWALKIE_SIGNAL_WIFI = 0,
    OWALKIE_SIGNAL_CELL = 1,
} owalkie_signal_mode;

/* --- managed sessions (requires OWALKIE_CORE_HAS_SESSION) --- */
owalkie_session_id owalkie_prepare_connection(
    const owalkie_connect_params* params,
    const owalkie_managed_callbacks* callbacks);

/**
 * Single connect attempt on a prepared session (teardown + TCP/WS). Call repeatedly after
 * @c OWALKIE_EV_CONNECTION_LOST until @c OWALKIE_EV_CONNECTED. @p timeout_ms TCP budget; 0 = ~3.5s.
 */
owalkie_result owalkie_connect(owalkie_session_id session_id, int timeout_ms);

void owalkie_disconnect(owalkie_session_id session_id);
void owalkie_disconnect_all(void);
/** Halt all sessions and wait up to @p timeout_ms for background teardown (app exit). */
void owalkie_disconnect_all_and_wait(int timeout_ms);
int owalkie_session_id_valid(owalkie_session_id session_id);
int owalkie_session_id_ready(owalkie_session_id session_id);

/**
 * Fills @p out_info for an active managed session.
 * @p opus_application_buf may be NULL; when set, copies negotiated Opus application string.
 */
owalkie_result owalkie_get_session_info(
    owalkie_session_id session_id,
    owalkie_session_info* out_info,
    char* opus_application_buf,
    size_t opus_application_buf_size);

/**
 * Enqueue an ordered uplink TX command (OPEN / PCM / OPUS / CLOSE / ABORT).
 * PCM and OPUS payloads are ignored unless @p op matches. CLOSE sends UDP EOF burst.
 */
owalkie_result owalkie_tx_submit(
    owalkie_session_id session_id,
    owalkie_tx_op op,
    const int16_t* pcm,
    size_t pcm_count,
    const uint8_t* opus,
    size_t opus_len);

/** Wait until queued TX commands are processed (before roger / CLOSE). */
int owalkie_tx_wait_idle(owalkie_session_id session_id, int timeout_ms);
owalkie_result owalkie_set_repeater_mode(owalkie_session_id session_id, int enabled);
void owalkie_set_power_profile(owalkie_session_id session_id, owalkie_power_profile profile);

/** Optional one-shot UDP NAT punch (keepalive/recovery runs automatically). */
owalkie_result owalkie_punch_nat(owalkie_session_id session_id);

/** Report link metrics; core maps to uplink signal byte 0..255 for all sessions. */
owalkie_result owalkie_report_signal(owalkie_signal_mode mode, int value);
owalkie_result owalkie_clear_signal(owalkie_signal_mode mode);
int owalkie_get_uplink_signal_byte(void);

#ifdef __cplusplus
}
#endif
