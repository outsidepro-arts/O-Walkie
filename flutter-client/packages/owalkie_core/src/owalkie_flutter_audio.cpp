#include "owalkie_flutter_audio.h"



#include "miniaudio.h"



#include <algorithm>

#include <atomic>

#include <cmath>

#include <cstring>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#endif



#ifdef __ANDROID__
#include <android/log.h>
#define OWALKIE_AUDIO_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "owalkie_audio", __VA_ARGS__)
#else
#define OWALKIE_AUDIO_LOGE(...) ((void)0)
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
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



std::mutex g_tx_mu;

std::condition_variable g_tx_cv;

std::thread g_tx_pump_thread;

std::atomic<bool> g_tx_pump_stop{false};

std::vector<int16_t> g_tx_fifo;

owalkie_flutter_audio::TxFrameCallback g_tx_cb = nullptr;

void* g_tx_user = nullptr;

std::atomic<bool> g_capture_active{false};

void tx_pump_loop();

void stop_tx_pump_thread_locked(std::thread* join_out = nullptr) {

    g_tx_pump_stop.store(true, std::memory_order_release);

    g_tx_cv.notify_all();

    if (g_tx_pump_thread.joinable()) {

        if (join_out) {

            *join_out = std::move(g_tx_pump_thread);

        } else {

            g_tx_pump_thread.join();

        }

    }

}

void start_tx_pump_thread_locked() {

    if (g_tx_pump_thread.joinable()) {

        return;

    }

    g_tx_pump_stop.store(false, std::memory_order_release);

    g_tx_pump_thread = std::thread(tx_pump_loop);

}

void tx_pump_loop() {

    std::vector<int16_t> frame;

    for (;;) {

        owalkie_flutter_audio::TxFrameCallback cb = nullptr;

        void* user = nullptr;

        {

            std::unique_lock<std::mutex> lock(g_tx_mu);

            g_tx_cv.wait(lock, [] {
                if (g_tx_pump_stop.load(std::memory_order_acquire)) {

                    return true;

                }

                if (!g_capture_active.load(std::memory_order_acquire)) {

                    return false;

                }

                return static_cast<int>(g_tx_fifo.size()) >= g_frame_samples;

            });

            if (g_tx_pump_stop.load(std::memory_order_acquire)) {

                break;

            }

            if (!g_capture_active.load(std::memory_order_acquire)) {

                continue;

            }

            const int frame_samples = g_frame_samples;

            if (frame_samples <= 0 || static_cast<int>(g_tx_fifo.size()) < frame_samples) {

                continue;

            }

            frame.assign(g_tx_fifo.begin(), g_tx_fifo.begin() + frame_samples);

            g_tx_fifo.erase(g_tx_fifo.begin(), g_tx_fifo.begin() + frame_samples);

            cb = g_tx_cb;

            user = g_tx_user;

        }

        if (cb && !frame.empty()) {

            cb(frame.data(), frame.size(), user);

        }

    }

}

int g_preferred_input_index = -1;
int g_preferred_output_index = -1;

#ifdef __ANDROID__
int g_platform_capture_id = -1;
int g_platform_playback_id = -1;
int g_aaudio_input_preset = 1; // ma_aaudio_input_preset_generic (MIC)
#endif

constexpr int kMaxListedDevices = 64;



void init_wasapi_com_once() {

#ifdef _WIN32

    static std::once_flag com_once;

    std::call_once(com_once, [] {

        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {

            CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        }

    });

#endif

}



float rx_gain() {

    const int p = g_rx_volume_percent.load(std::memory_order_relaxed);

    return static_cast<float>(std::clamp(p, 0, 200)) / 100.0f;

}



void ensure_context() {

    if (g_context_inited) {

        return;

    }

#ifdef _WIN32

    init_wasapi_com_once();

#endif

    ma_context_config ctx_cfg = ma_context_config_init();

#if defined(__APPLE__) && TARGET_OS_IPHONE

    ctx_cfg.coreaudio.noAudioSessionActivate = MA_TRUE;

    ctx_cfg.coreaudio.noAudioSessionDeactivate = MA_TRUE;

    ctx_cfg.coreaudio.sessionCategory = ma_ios_session_category_none;

#endif

    if (ma_context_init(nullptr, 0, &ctx_cfg, &g_context) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("ma_context_init failed");

        return;

    }

    g_context_inited = true;

}



const ma_device_id* resolve_device_id_from_list(
    ma_device_info* infos,
    ma_uint32 count,
    int preferred_index) {

    if (!infos || count == 0) {

        return nullptr;

    }

    if (preferred_index >= 0 && static_cast<ma_uint32>(preferred_index) < count) {

        return &infos[preferred_index].id;

    }

    for (ma_uint32 i = 0; i < count; ++i) {

        if (infos[i].isDefault) {

            return &infos[i].id;

        }

    }

    return &infos[0].id;

}



const ma_device_id* resolve_playback_device_id_locked() {

#ifdef __ANDROID__

    if (g_platform_playback_id >= 0) {

        static ma_device_id id{};

        id.aaudio = g_platform_playback_id;

        return &id;

    }

#endif

    ma_device_info* play = nullptr;

    ma_uint32 nPlay = 0;

    if (ma_context_get_devices(&g_context, &play, &nPlay, nullptr, nullptr) != MA_SUCCESS) {

        return nullptr;

    }

    return resolve_device_id_from_list(play, nPlay, g_preferred_output_index);

}



const ma_device_id* resolve_capture_device_id_locked() {

#ifdef __ANDROID__

    if (g_platform_capture_id >= 0) {

        static ma_device_id id{};

        id.aaudio = g_platform_capture_id;

        return &id;

    }

#endif

    ma_device_info* caps = nullptr;

    ma_uint32 nCap = 0;

    if (ma_context_get_devices(&g_context, nullptr, nullptr, &caps, &nCap) != MA_SUCCESS) {

        return nullptr;

    }

    return resolve_device_id_from_list(caps, nCap, g_preferred_input_index);

}



void copy_device_name(char* dst, size_t dst_size, const char* src) {

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        dst[0] = '\0';

        return;

    }

    std::strncpy(dst, src, dst_size - 1);

    dst[dst_size - 1] = '\0';

}



int32_t fill_device_list_locked(
    ma_device_info* infos,
    ma_uint32 count,
    owalkie_flutter_audio::AudioDeviceEntry* out,
    int32_t max_count) {

    if (!out || max_count <= 0 || !infos) {

        return 0;

    }

    ma_uint32 n = count;

    if (static_cast<ma_uint32>(max_count) < n) {

        n = static_cast<ma_uint32>(max_count);

    }

    if (static_cast<ma_uint32>(kMaxListedDevices) < n) {

        n = static_cast<ma_uint32>(kMaxListedDevices);

    }

    for (ma_uint32 i = 0; i < n; ++i) {

        out[i].index = static_cast<int32_t>(i);

        out[i].is_default = infos[i].isDefault ? 1 : 0;

        copy_device_name(out[i].name, sizeof(out[i].name), infos[i].name);

    }

    return static_cast<int32_t>(n);

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

    std::thread pump_join;

    {

        std::lock_guard<std::mutex> tx_lock(g_tx_mu);

        stop_tx_pump_thread_locked(&pump_join);

        g_tx_fifo.clear();

    }

    if (pump_join.joinable()) {

        pump_join.join();

    }

    if (g_capture_open) {

        ma_device_uninit(&g_capture);

        g_capture_open = false;

    }

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

    if (!g_capture_active.load(std::memory_order_acquire) || !input || frame_count == 0) {

        return;

    }

    const auto* samples = static_cast<const int16_t*>(input);

    {

        std::lock_guard<std::mutex> lock(g_tx_mu);

        if (!g_capture_active.load(std::memory_order_acquire)) {

            return;

        }

        g_tx_fifo.insert(g_tx_fifo.end(), samples, samples + frame_count);

    }

    g_tx_cv.notify_one();

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

    cfg.playback.pDeviceID = resolve_playback_device_id_locked();

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

    cfg.capture.pDeviceID = resolve_capture_device_id_locked();

    cfg.capture.format = ma_format_s16;

    cfg.capture.channels = 1;

    cfg.sampleRate = static_cast<ma_uint32>(g_sample_rate);

    cfg.dataCallback = capture_cb;

    cfg.periodSizeInFrames = static_cast<ma_uint32>(g_frame_samples > 0 ? g_frame_samples : 160);

#ifdef __ANDROID__

    cfg.aaudio.usage = ma_aaudio_usage_voice_communication;

    cfg.aaudio.inputPreset = static_cast<ma_aaudio_input_preset>(g_aaudio_input_preset);

#endif

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

    stop_local_pcm_loop();

    std::lock_guard<std::mutex> lock(g_mu);

    close_capture_locked();

    close_playback_locked();

    {

        std::lock_guard<std::mutex> tx_lock(g_tx_mu);

        g_tx_cb = nullptr;

        g_tx_user = nullptr;

    }

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

    {

        std::lock_guard<std::mutex> tx_lock(g_tx_mu);

        g_tx_fifo.clear();

        start_tx_pump_thread_locked();

    }

    g_capture_active.store(true, std::memory_order_release);

    return true;

}



void stop_capture() {

    g_capture_active.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> tx_lock(g_tx_mu);

    g_tx_fifo.clear();

}



void set_tx_frame_callback(TxFrameCallback cb, void* user) {

    std::lock_guard<std::mutex> lock(g_tx_mu);

    g_tx_cb = cb;

    g_tx_user = user;

}



struct LocalPlayState {

    const int16_t* samples = nullptr;

    size_t position = 0;

    size_t total = 0;

};



void local_play_data_cb(
    ma_device* device,
    void* output,
    const void* /*input*/,
    ma_uint32 frame_count) {

    auto* st = static_cast<LocalPlayState*>(device->pUserData);

    auto* out = static_cast<int16_t*>(output);

    for (ma_uint32 i = 0; i < frame_count; ++i) {

        if (st->position < st->total) {

            out[i] = st->samples[st->position++];

        } else {

            out[i] = 0;

        }

    }

}



void play_local_pcm_blocking(const int16_t* samples, size_t count, int sample_rate_hz) {

    if (!samples || count == 0 || sample_rate_hz <= 0) {

        return;

    }

    std::unique_lock<std::mutex> lock(g_mu);

    ensure_context();

    if (!g_context_inited) {

        return;

    }

    LocalPlayState state{samples, 0, count};

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);

    cfg.playback.pDeviceID = resolve_playback_device_id_locked();

    cfg.playback.format = ma_format_s16;

    cfg.playback.channels = 1;

    cfg.sampleRate = static_cast<ma_uint32>(sample_rate_hz);

    cfg.dataCallback = local_play_data_cb;

    cfg.pUserData = &state;

    ma_device dev{};

    if (ma_device_init(&g_context, &cfg, &dev) != MA_SUCCESS) {

        OWALKIE_AUDIO_LOGE("local play ma_device_init failed");

        return;

    }

    if (ma_device_start(&dev) != MA_SUCCESS) {

        ma_device_uninit(&dev);

        return;

    }

    const auto duration_ms =

        static_cast<int>((count * 1000LL) / sample_rate_hz) + 80;

    lock.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));

    lock.lock();

    ma_device_stop(&dev);

    ma_device_uninit(&dev);

}



namespace {



std::mutex g_local_pcm_loop_mu;

std::thread g_local_pcm_loop_thread;

std::atomic<bool> g_local_pcm_loop_stop{true};



struct LoopPlayState {

    const int16_t* samples = nullptr;

    size_t total = 0;

    size_t position = 0;

    std::atomic<bool>* stop_flag = nullptr;

};



void local_pcm_loop_data_cb(
    ma_device* device,
    void* output,
    const void* /*input*/,
    ma_uint32 frame_count) {

    auto* st = static_cast<LoopPlayState*>(device->pUserData);

    auto* out = static_cast<int16_t*>(output);

    const ma_uint32 bpf = ma_get_bytes_per_frame(ma_format_s16, 1);

    if (!st || !st->samples || st->total == 0 ||
        (st->stop_flag && st->stop_flag->load(std::memory_order_acquire))) {

        std::memset(output, 0, frame_count * bpf);

        return;

    }

    for (ma_uint32 i = 0; i < frame_count; ++i) {

        out[i] = st->samples[st->position % st->total];

        st->position++;

    }

}



void join_local_pcm_loop_thread_locked() {

    g_local_pcm_loop_stop.store(true, std::memory_order_release);

    if (g_local_pcm_loop_thread.joinable()) {

        g_local_pcm_loop_thread.join();

    }

}



} // namespace



void start_local_pcm_loop(const int16_t* samples, size_t count, int sample_rate_hz) {

    if (!samples || count == 0 || sample_rate_hz <= 0) {

        return;

    }

    auto pcm = std::make_shared<std::vector<int16_t>>(samples, samples + count);

    std::lock_guard<std::mutex> lock(g_local_pcm_loop_mu);

    join_local_pcm_loop_thread_locked();

    g_local_pcm_loop_stop.store(false, std::memory_order_release);

    g_local_pcm_loop_thread = std::thread([pcm, sample_rate_hz]() {

        LoopPlayState state{pcm->data(), pcm->size(), 0, &g_local_pcm_loop_stop};

        ma_device dev{};

        bool dev_open = false;

        {

            std::lock_guard<std::mutex> audio_lock(g_mu);

            ensure_context();

            if (!g_context_inited) {

                return;

            }

            ma_device_config cfg = ma_device_config_init(ma_device_type_playback);

            cfg.playback.pDeviceID = resolve_playback_device_id_locked();

            cfg.playback.format = ma_format_s16;

            cfg.playback.channels = 1;

            cfg.sampleRate = static_cast<ma_uint32>(sample_rate_hz);

            cfg.dataCallback = local_pcm_loop_data_cb;

            cfg.pUserData = &state;

            if (ma_device_init(&g_context, &cfg, &dev) != MA_SUCCESS) {

                OWALKIE_AUDIO_LOGE("local pcm loop ma_device_init failed");

                return;

            }

            if (ma_device_start(&dev) != MA_SUCCESS) {

                ma_device_uninit(&dev);

                return;

            }

            dev_open = true;

        }

        while (!g_local_pcm_loop_stop.load(std::memory_order_acquire)) {

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        }

        if (dev_open) {

            std::lock_guard<std::mutex> audio_lock(g_mu);

            ma_device_stop(&dev);

            ma_device_uninit(&dev);

        }

    });

}



void stop_local_pcm_loop() {

    std::lock_guard<std::mutex> lock(g_local_pcm_loop_mu);

    join_local_pcm_loop_thread_locked();

}



int32_t list_capture_devices(AudioDeviceEntry* out, int32_t max_count) {

    if (!out || max_count <= 0) {

        return 0;

    }

#ifdef __ANDROID__

    (void)out;

    (void)max_count;

    return 0;

#else

    init_wasapi_com_once();

    ma_context ctx{};

    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {

        return 0;

    }

    ma_device_info* caps = nullptr;

    ma_uint32 nCap = 0;

    int32_t written = 0;

    if (ma_context_get_devices(&ctx, nullptr, nullptr, &caps, &nCap) == MA_SUCCESS) {

        written = fill_device_list_locked(caps, nCap, out, max_count);

    }

    ma_context_uninit(&ctx);

    return written;

#endif

}



int32_t list_playback_devices(AudioDeviceEntry* out, int32_t max_count) {

    if (!out || max_count <= 0) {

        return 0;

    }

#ifdef __ANDROID__

    (void)out;

    (void)max_count;

    return 0;

#else

    init_wasapi_com_once();

    ma_context ctx{};

    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {

        return 0;

    }

    ma_device_info* play = nullptr;

    ma_uint32 nPlay = 0;

    int32_t written = 0;

    if (ma_context_get_devices(&ctx, &play, &nPlay, nullptr, nullptr) == MA_SUCCESS) {

        written = fill_device_list_locked(play, nPlay, out, max_count);

    }

    ma_context_uninit(&ctx);

    return written;

#endif

}



void set_capture_device_index(int32_t index) {

    std::lock_guard<std::mutex> lock(g_mu);

    if (g_preferred_input_index == index) {

        return;

    }

    g_preferred_input_index = index;

#ifdef __ANDROID__

    g_platform_capture_id = -1;

#endif

    const bool was_active = g_capture_active.load(std::memory_order_relaxed);

    close_capture_locked();

    if (was_active) {

        open_capture_locked();

        g_capture_active.store(true, std::memory_order_release);

    }

}



void set_playback_device_index(int32_t index) {

    std::lock_guard<std::mutex> lock(g_mu);

    if (g_preferred_output_index == index) {

        return;

    }

    g_preferred_output_index = index;

#ifdef __ANDROID__

    g_platform_playback_id = -1;

#endif

    close_playback_locked();

}



void set_capture_platform_device_id(int32_t platform_id) {

#ifndef __ANDROID__

    (void)platform_id;

    return;

#else

    std::lock_guard<std::mutex> lock(g_mu);

    if (g_platform_capture_id == platform_id) {

        return;

    }

    g_platform_capture_id = platform_id;

    g_preferred_input_index = -1;

    const bool was_active = g_capture_active.load(std::memory_order_relaxed);

    close_capture_locked();

    if (was_active) {

        open_capture_locked();

        g_capture_active.store(true, std::memory_order_release);

    }

#endif

}



void set_capture_aaudio_input_preset(int32_t preset) {

#ifndef __ANDROID__

    (void)preset;

    return;

#else

    std::lock_guard<std::mutex> lock(g_mu);

    const int clamped = preset < 0 ? 0 : (preset > 6 ? 6 : preset);

    if (g_aaudio_input_preset == clamped) {

        return;

    }

    g_aaudio_input_preset = clamped;

    g_platform_capture_id = -1;

    const bool was_active = g_capture_active.load(std::memory_order_relaxed);

    close_capture_locked();

    if (was_active) {

        open_capture_locked();

        g_capture_active.store(true, std::memory_order_release);

    }

#endif

}



void set_playback_platform_device_id(int32_t platform_id) {

#ifndef __ANDROID__

    (void)platform_id;

    return;

#else

    std::lock_guard<std::mutex> lock(g_mu);

    if (g_platform_playback_id == platform_id) {

        return;

    }

    g_platform_playback_id = platform_id;

    g_preferred_output_index = -1;

    close_playback_locked();

#endif

}



int32_t capture_device_index() {

    std::lock_guard<std::mutex> lock(g_mu);

    return g_preferred_input_index;

}



int32_t playback_device_index() {

    std::lock_guard<std::mutex> lock(g_mu);

    return g_preferred_output_index;

}



} // namespace owalkie_flutter_audio

