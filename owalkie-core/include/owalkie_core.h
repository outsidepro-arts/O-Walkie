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
} owalkie_result;

typedef struct owalkie_session owalkie_session_t;

typedef enum owalkie_event_type {
    OWALKIE_EV_CONNECTING = 0,
    OWALKIE_EV_CONNECTED,
    OWALKIE_EV_DISCONNECTED,
    OWALKIE_EV_PROTOCOL_ERROR,

    OWALKIE_EV_WELCOME,

    OWALKIE_EV_RX_BROADCAST_START,
    OWALKIE_EV_RX_BROADCAST_END,

    OWALKIE_EV_LOCAL_TX_START,
    OWALKIE_EV_LOCAL_TX_END,

    OWALKIE_EV_PTT_LOCKED,
    OWALKIE_EV_PTT_UNLOCKED,

    OWALKIE_EV_TX_COUNTDOWN_START,
    OWALKIE_EV_TX_STOP,

    OWALKIE_EV_UDP_TRANSPORT_READY,
    OWALKIE_EV_UDP_TRANSPORT_LOST,
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

typedef struct owalkie_session_state {
    int connected;
    int udp_ready;
    int receiving;
    int local_tx_active;
    int ptt_server_locked;
    int ptt_lock_display_sec;
    owalkie_welcome_config config;
    int has_welcome;
} owalkie_session_state;

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

typedef void (*owalkie_on_session_event_fn)(void* user_data, const owalkie_event* event);

typedef struct owalkie_session_callbacks {
    owalkie_on_rx_pcm_fn on_rx_pcm;
    owalkie_on_session_event_fn on_session_event;
    void* user_data;
} owalkie_session_callbacks;

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

owalkie_result owalkie_json_build_join(
    const char* channel,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written);

owalkie_result owalkie_json_build_udp_hello(
    int local_udp_port,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written);

owalkie_result owalkie_json_build_repeater_mode(
    int enabled,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written);

owalkie_result owalkie_json_build_heartbeat(
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written);

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

/* --- session (requires OWALKIE_CORE_HAS_SESSION) --- */
owalkie_result owalkie_session_create(owalkie_session_t** out_session);
void owalkie_session_destroy(owalkie_session_t* session);

void owalkie_session_set_callbacks(
    owalkie_session_t* session,
    const owalkie_session_callbacks* callbacks);

owalkie_result owalkie_session_connect(
    owalkie_session_t* session,
    const owalkie_connect_params* params);

void owalkie_session_disconnect(owalkie_session_t* session);

int owalkie_session_is_connected(const owalkie_session_t* session);

void owalkie_session_set_auto_reconnect(owalkie_session_t* session, int enabled);
int owalkie_session_auto_reconnect_enabled(const owalkie_session_t* session);

owalkie_result owalkie_session_feed_tx_pcm(
    owalkie_session_t* session,
    const int16_t* samples,
    size_t sample_count);

owalkie_result owalkie_session_send_tx_eof_burst(owalkie_session_t* session);

owalkie_result owalkie_session_set_repeater_mode(owalkie_session_t* session, int enabled);

owalkie_result owalkie_session_reset_udp_transport(owalkie_session_t* session);

void owalkie_session_get_state(
    const owalkie_session_t* session,
    owalkie_session_state* out_state);

typedef enum owalkie_power_profile {
    OWALKIE_POWER_FOREGROUND = 0,
    OWALKIE_POWER_BACKGROUND,
    OWALKIE_POWER_ACTIVE_TX,
} owalkie_power_profile;

void owalkie_session_set_power_profile(owalkie_session_t* session, owalkie_power_profile profile);
owalkie_power_profile owalkie_session_get_power_profile(const owalkie_session_t* session);

void owalkie_session_enter_udp_recovery(owalkie_session_t* session);
void owalkie_session_notify_network_changed(owalkie_session_t* session);
owalkie_result owalkie_session_punch_udp_nat(owalkie_session_t* session);

/* Uplink signal byte for outgoing audio UDP frames (0..255, except 254). Default: 255. */
owalkie_result owalkie_session_set_tx_signal_strength(
    owalkie_session_t* session,
    int strength_0_255);
int owalkie_session_get_tx_signal_strength(const owalkie_session_t* session);

#ifdef __cplusplus
}
#endif
