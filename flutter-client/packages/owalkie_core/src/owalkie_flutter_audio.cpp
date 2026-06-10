#include "owalkie_flutter_audio.h"



#include "miniaudio.h"



#include <algorithm>

#include <atomic>

#include <cmath>

#include <cstring>

#include <mutex>

#include <vector>



#ifdef __ANDROID__
#include <android/log.h>
#define OWALKIE_AUDIO_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "owalkie_audio", __VA_ARGS__)
#else
#define OWALKIE_AUDIO_LOGE(...) ((void)0)
#endif



namespace {



std::mutex g_mu;

ma_context g_context{};

bool g_context_inited = false;

ma_device g_playback{};

ma_device g_capture{};

bool g_playback_open = false;

bool g_capture_open = false;

ma_pcm_rb g_rx_rb{};

bool g_rx_rb_inited = false;



int g_sample_rate = 8000;

int g_packet_ms = 20;

int g_frame_samples = 160;

std::atomic<int> g_rx_volume_percent{100};



std::vector<int16_t> g_tx_fifo;

owalkie_flutter_audio::TxFrameCallback g_tx_cb = nullptr;

void* g_tx_user = nullptr;

std::atomic<bool> g_capture_active{false};



float rx_gain() {

    const int p = g_rx_volume_percent.load(std::memory_order_relaxed);

    return static_cast<float>(std::clamp(p, 0, 200)) / 100.0f;

}



void ensure_context() {

    if (g_context_inited) {

        return;

    }

    if (ma_context_init(nullptr, 0, nullptr, &g_context) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("ma_context_init failed");

        return;

    }

    g_context_inited = true;

}



void close_playback_locked() {

    if (g_playback_open) {

        ma_device_uninit(&g_playback);

        g_playback_open = false;

    }

    if (g_rx_rb_inited) {

        ma_pcm_rb_uninit(&g_rx_rb);

        g_rx_rb_inited = false;

    }

}



void close_capture_locked() {

    g_capture_active.store(false, std::memory_order_release);

    if (g_capture_open) {

        ma_device_uninit(&g_capture);

        g_capture_open = false;

    }

    g_tx_fifo.clear();

}



void playback_cb(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {

    (void)device;

    (void)input;

    auto* out = static_cast<int16_t*>(output);

    const ma_uint32 bpf = ma_get_bytes_per_frame(ma_format_s16, 1);

    ma_uint32 done = 0;

    while (done < frame_count) {

        ma_uint32 chunk = frame_count - done;

        void* rb = nullptr;

        ma_pcm_rb_acquire_read(&g_rx_rb, &chunk, &rb);

        if (chunk == 0) {

            break;

        }

        const float gain = rx_gain();

        const auto* src = static_cast<const int16_t*>(rb);

        for (ma_uint32 i = 0; i < chunk; ++i) {

            const float v = static_cast<float>(src[i]) * gain;

            out[done + i] = static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f));

        }

        ma_pcm_rb_commit_read(&g_rx_rb, chunk);

        done += chunk;

    }

    if (done < frame_count) {

        std::memset(reinterpret_cast<uint8_t*>(out) + done * bpf, 0, (frame_count - done) * bpf);

    }

}



void capture_cb(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {

    (void)device;

    (void)output;

    if (!g_capture_active.load(std::memory_order_acquire) || !input) {

        return;

    }

    const auto* samples = static_cast<const int16_t*>(input);

    const size_t count = frame_count;

    std::vector<int16_t> frame;

    {

        std::lock_guard<std::mutex> lock(g_mu);

        owalkie_flutter_audio::TxFrameCallback cb = g_tx_cb;

        void* user = g_tx_user;

        g_tx_fifo.insert(g_tx_fifo.end(), samples, samples + count);

        while (static_cast<int>(g_tx_fifo.size()) >= g_frame_samples) {

            frame.assign(g_tx_fifo.begin(), g_tx_fifo.begin() + g_frame_samples);

            g_tx_fifo.erase(g_tx_fifo.begin(), g_tx_fifo.begin() + g_frame_samples);

            if (cb) {

                cb(frame.data(), frame.size(), user);

            }

        }

    }

}



bool open_playback_locked() {

    if (g_playback_open) {

        return true;

    }

    ensure_context();

    if (!g_context_inited) {

        return false;

    }

    const ma_uint32 capacity_frames = static_cast<ma_uint32>(g_sample_rate);

    if (ma_pcm_rb_init(ma_format_s16, 1, capacity_frames, nullptr, nullptr, &g_rx_rb) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("ma_pcm_rb_init failed");

        return false;

    }

    g_rx_rb_inited = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);

    cfg.playback.format = ma_format_s16;

    cfg.playback.channels = 1;

    cfg.sampleRate = static_cast<ma_uint32>(g_sample_rate);

    cfg.dataCallback = playback_cb;

    if (ma_device_init(&g_context, &cfg, &g_playback) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("playback ma_device_init failed (%d Hz)", g_sample_rate);

        ma_pcm_rb_uninit(&g_rx_rb);

        g_rx_rb_inited = false;

        return false;

    }

    if (ma_device_start(&g_playback) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("playback ma_device_start failed");

        ma_device_uninit(&g_playback);

        ma_pcm_rb_uninit(&g_rx_rb);

        g_rx_rb_inited = false;

        return false;

    }

    g_playback_open = true;

    return true;

}



bool open_capture_locked() {

    if (g_capture_open) {

        return true;

    }

    ensure_context();

    if (!g_context_inited) {

        return false;

    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);

    cfg.capture.format = ma_format_s16;

    cfg.capture.channels = 1;

    cfg.sampleRate = static_cast<ma_uint32>(g_sample_rate);

    cfg.dataCallback = capture_cb;

    if (ma_device_init(&g_context, &cfg, &g_capture) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("capture ma_device_init failed (%d Hz)", g_sample_rate);

        return false;

    }

    if (ma_device_start(&g_capture) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("capture ma_device_start failed");

        ma_device_uninit(&g_capture);

        return false;

    }

    g_capture_open = true;

    return true;

}



void configure_locked(int sample_rate_hz, int packet_ms) {

    const bool reopen = g_sample_rate != sample_rate_hz || g_packet_ms != packet_ms;

    g_sample_rate = sample_rate_hz > 0 ? sample_rate_hz : 8000;

    g_packet_ms = packet_ms > 0 ? packet_ms : 20;

    g_frame_samples = (g_sample_rate * g_packet_ms) / 1000;

    if (g_frame_samples <= 0) {

        g_frame_samples = 160;

    }

    if (reopen) {

        close_capture_locked();

        close_playback_locked();

    }

    (void)open_playback_locked();

}



} // namespace



namespace owalkie_flutter_audio {



void shutdown() {

    std::lock_guard<std::mutex> lock(g_mu);

    close_capture_locked();

    close_playback_locked();

    g_tx_cb = nullptr;

    g_tx_user = nullptr;

    if (g_context_inited) {

        ma_context_uninit(&g_context);

        g_context_inited = false;

    }

}



void configure(int sample_rate_hz, int packet_ms) {

    std::lock_guard<std::mutex> lock(g_mu);

    configure_locked(sample_rate_hz, packet_ms);

}



void set_rx_volume_percent(int percent) {

    g_rx_volume_percent.store(std::clamp(percent, 0, 200), std::memory_order_relaxed);

}



void on_rx_pcm(const int16_t* samples, size_t count, int sample_rate_hz) {

    if (!samples || count == 0) {

        return;

    }

    std::lock_guard<std::mutex> lock(g_mu);

    if (sample_rate_hz > 0 && sample_rate_hz != g_sample_rate) {

        configure_locked(sample_rate_hz, g_packet_ms);

    }

    if (!open_playback_locked()) {

        return;

    }

    ma_uint32 frames = static_cast<ma_uint32>(count);

    const int16_t* src = samples;

    while (frames > 0) {

        ma_uint32 chunk = frames;

        void* rb = nullptr;

        if (ma_pcm_rb_acquire_write(&g_rx_rb, &chunk, &rb) != MA_SUCCESS || chunk == 0) {

            break;

        }

        std::memcpy(rb, src, chunk * sizeof(int16_t));

        ma_pcm_rb_commit_write(&g_rx_rb, chunk);

        src += chunk;

        frames -= chunk;

    }

}



bool start_capture() {

    std::lock_guard<std::mutex> lock(g_mu);

    if (!open_capture_locked()) {

        return false;

    }

    g_tx_fifo.clear();

    g_capture_active.store(true, std::memory_order_release);

    return true;

}



void stop_capture() {

    std::lock_guard<std::mutex> lock(g_mu);

    g_capture_active.store(false, std::memory_order_release);

    g_tx_fifo.clear();

}



void set_tx_frame_callback(TxFrameCallback cb, void* user) {

    std::lock_guard<std::mutex> lock(g_mu);

    g_tx_cb = cb;

    g_tx_user = user;

}



} // namespace owalkie_flutter_audio

