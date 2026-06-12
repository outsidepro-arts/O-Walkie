#pragma once

#include <cstddef>
#include <cstdint>

namespace owalkie_flutter_audio {

struct AudioDeviceEntry {
    int32_t index;
    int32_t is_default;
    char name[256];
};

void shutdown();
void configure(int sample_rate_hz, int packet_ms);
void set_rx_volume_percent(int percent);
void on_rx_pcm(const int16_t* samples, size_t count, int sample_rate_hz);

bool start_capture();
void stop_capture();

using TxFrameCallback = void (*)(const int16_t* frame, size_t sample_count, void* user);
void set_tx_frame_callback(TxFrameCallback cb, void* user);

/** One-shot local speaker playback (UI tones / roger preview). Blocks until done. */
void play_local_pcm_blocking(const int16_t* samples, size_t count, int sample_rate_hz);

/** Loop @p samples on a background playback device until [stop_local_pcm_loop]. */
void start_local_pcm_loop(const int16_t* samples, size_t count, int sample_rate_hz);

void stop_local_pcm_loop();

int32_t list_capture_devices(AudioDeviceEntry* out, int32_t max_count);
int32_t list_playback_devices(AudioDeviceEntry* out, int32_t max_count);
void set_capture_device_index(int32_t index);
void set_playback_device_index(int32_t index);
/** Android: AAudio input preset (ma_aaudio_input_preset). No-op on other platforms. */
void set_capture_aaudio_input_preset(int32_t preset);
void set_capture_platform_device_id(int32_t platform_id);
void set_playback_platform_device_id(int32_t platform_id);
int32_t capture_device_index();
int32_t playback_device_index();

} // namespace owalkie_flutter_audio
