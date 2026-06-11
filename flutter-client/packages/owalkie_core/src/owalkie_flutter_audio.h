#pragma once

#include <cstddef>
#include <cstdint>

namespace owalkie_flutter_audio {

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

} // namespace owalkie_flutter_audio
