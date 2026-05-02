#include "AudioEngine.h"

#include "RelayClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <opus/opus.h>

#ifdef _WIN32
#include <windows.h>
#endif

void AudioEngine::CaptureCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    if (self) {
        self->OnCaptureFrames(pInput, frameCount);
    }
}

void AudioEngine::PlaybackCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    if (!self || !pOutput) {
        return;
    }
    const ma_uint32 bpf = ma_get_bytes_per_frame(ma_format_s16, 1);
    ma_uint8* out = static_cast<ma_uint8*>(pOutput);
    ma_uint32 done = 0;
    while (done < frameCount) {
        ma_uint32 chunk = frameCount - done;
        void* rb = nullptr;
        ma_pcm_rb_acquire_read(&self->playbackRb_, &chunk, &rb);
        if (chunk == 0) {
            break;
        }
        std::memcpy(out + done * bpf, rb, chunk * bpf);
        ma_pcm_rb_commit_read(&self->playbackRb_, chunk);
        done += chunk;
    }
    if (done < frameCount) {
        std::memset(out + done * bpf, 0, (frameCount - done) * bpf);
    }
}

namespace {
constexpr int kRogerTailMs = 40;
constexpr int kUiSignalPlaybackRate = 48000;
constexpr int kLocalSignalSynthesisRate = 48000;
constexpr int kRxVolumeMinPercent = 0;
constexpr int kRxVolumeMaxPercent = 200;
constexpr int kRxVolumeDefaultPercent = 100;

struct WavPcm {
    std::vector<int16_t> samples;
    int sampleRate = 0;
};

std::filesystem::path ExecutableDir() {
#ifdef _WIN32
    char pathBuf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, pathBuf, static_cast<DWORD>(sizeof(pathBuf)));
    if (n > 0) {
        return std::filesystem::path(std::string(pathBuf, pathBuf + n)).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

std::vector<uint8_t> ReadBinary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> out(sz);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
    }
    return out;
}

int32_t ReadLeI32(const uint8_t* p) {
    return static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16) |
           (static_cast<int32_t>(p[3]) << 24);
}

int16_t ReadLeI16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

WavPcm ParseWavPcm(const std::vector<uint8_t>& bytes) {
    WavPcm out{};
    if (bytes.size() < 44) {
        return out;
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return out;
    }
    size_t offset = 12;
    int sampleRate = 0;
    int channels = 0;
    int bits = 0;
    size_t dataStart = 0;
    size_t dataSize = 0;
    while (offset + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + offset;
        const int32_t chunkSize = ReadLeI32(chunk + 4);
        const size_t payloadStart = offset + 8;
        if (chunkSize < 0 || payloadStart + static_cast<size_t>(chunkSize) > bytes.size()) {
            break;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint8_t* p = bytes.data() + payloadStart;
            const int audioFmt = ReadLeI16(p + 0);
            channels = ReadLeI16(p + 2);
            sampleRate = ReadLeI32(p + 4);
            bits = ReadLeI16(p + 14);
            if (audioFmt != 1) { // PCM
                return {};
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataStart = payloadStart;
            dataSize = static_cast<size_t>(chunkSize);
            break;
        }
        offset = payloadStart + static_cast<size_t>(chunkSize) + (static_cast<size_t>(chunkSize) & 1u);
    }
    if (sampleRate <= 0 || channels != 1 || bits != 16 || dataSize < 2 || dataStart + dataSize > bytes.size()) {
        return {};
    }
    out.sampleRate = sampleRate;
    out.samples.reserve(dataSize / 2);
    for (size_t i = 0; i + 1 < dataSize; i += 2) {
        out.samples.push_back(ReadLeI16(bytes.data() + dataStart + i));
    }
    return out;
}

std::vector<int16_t> ResampleLinear(const std::vector<int16_t>& in, int srcRate, int dstRate) {
    if (in.empty() || srcRate <= 0 || dstRate <= 0) {
        return {};
    }
    if (srcRate == dstRate) {
        return in;
    }
    const size_t outN = std::max<size_t>(1, (in.size() * static_cast<size_t>(dstRate)) / static_cast<size_t>(srcRate));
    std::vector<int16_t> out(outN);
    const double scale = static_cast<double>(srcRate) / static_cast<double>(dstRate);
    for (size_t i = 0; i < outN; ++i) {
        const double srcPos = i * scale;
        const size_t i0 = static_cast<size_t>(srcPos);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const double t = srcPos - static_cast<double>(i0);
        const double v = (1.0 - t) * in[i0] + t * in[i1];
        out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(std::lround(v)), -32768, 32767));
    }
    return out;
}

std::vector<int16_t> LoadSoundFromExeDir(const char* baseName, int targetRate) {
    if (!baseName || targetRate <= 0) {
        return {};
    }
    const auto dir = ExecutableDir() / "sounds";
    const std::filesystem::path candidates[] = {dir / (std::string(baseName) + ".wav"), dir / baseName};
    for (const auto& p : candidates) {
        const auto bytes = ReadBinary(p);
        if (bytes.empty()) {
            continue;
        }
        auto wav = ParseWavPcm(bytes);
        if (wav.samples.empty()) {
            continue;
        }
        return ResampleLinear(wav.samples, wav.sampleRate, targetRate);
    }
    return {};
}

struct OneShotPlaybackState {
    std::vector<int16_t> pcm;
    size_t offset = 0;
};

void OneShotPlaybackCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    auto* state = static_cast<OneShotPlaybackState*>(pDevice->pUserData);
    auto* out = static_cast<int16_t*>(pOutput);
    if (!state || !out) {
        return;
    }
    size_t framesLeft = state->pcm.size() - state->offset;
    const size_t toCopy = std::min<size_t>(frameCount, framesLeft);
    if (toCopy > 0) {
        std::memcpy(out, state->pcm.data() + state->offset, toCopy * sizeof(int16_t));
        state->offset += toCopy;
    }
    if (toCopy < frameCount) {
        std::memset(out + toCopy, 0, (frameCount - toCopy) * sizeof(int16_t));
    }
}

void PlayOneShotHighQuality(const std::vector<int16_t>& pcm, int srcRate) {
    if (pcm.empty() || srcRate <= 0) {
        return;
    }
    auto pcm44 = ResampleLinear(pcm, srcRate, kUiSignalPlaybackRate);
    if (pcm44.empty()) {
        return;
    }
    std::thread([pcm44 = std::move(pcm44)]() mutable {
        ma_context ctx{};
        if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
            return;
        }
        auto state = std::make_unique<OneShotPlaybackState>();
        state->pcm = std::move(pcm44);
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_s16;
        cfg.playback.channels = 1;
        cfg.sampleRate = static_cast<ma_uint32>(kUiSignalPlaybackRate);
        cfg.dataCallback = OneShotPlaybackCallback;
        cfg.pUserData = state.get();
        ma_device dev{};
        if (ma_device_init(&ctx, &cfg, &dev) != MA_SUCCESS) {
            ma_context_uninit(&ctx);
            return;
        }
        if (ma_device_start(&dev) != MA_SUCCESS) {
            ma_device_uninit(&dev);
            ma_context_uninit(&ctx);
            return;
        }
        const auto playMs = static_cast<int64_t>(state->pcm.size()) * 1000LL / kUiSignalPlaybackRate;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max<int64_t>(playMs + 50, 80)));
        ma_device_stop(&dev);
        ma_device_uninit(&dev);
        ma_context_uninit(&ctx);
    }).detach();
}

void AppendSilence(std::vector<int16_t>& out, int sampleRate, int durationMs) {
    if (sampleRate <= 0 || durationMs <= 0) {
        return;
    }
    const int samples = (sampleRate * durationMs) / 1000;
    out.insert(out.end(), static_cast<size_t>(samples), 0);
}

void AppendTone(std::vector<int16_t>& out, int sampleRate, double freqHz, int durationMs, double gain = 0.35) {
    if (sampleRate <= 0 || durationMs <= 0 || freqHz <= 0.0) {
        return;
    }
    const int samples = (sampleRate * durationMs) / 1000;
    constexpr double kPi = 3.14159265358979323846;
    const double w = (2.0 * kPi * freqHz) / static_cast<double>(sampleRate);
    for (int i = 0; i < samples; ++i) {
        const double env = std::sin((kPi * i) / std::max(samples - 1, 1));
        const double s = std::sin(w * i) * env * gain;
        const int v = static_cast<int>(std::lround(s * 32767.0));
        out.push_back(static_cast<int16_t>(std::clamp(v, -32768, 32767)));
    }
}

std::vector<int16_t> ApplyGain(const std::vector<int16_t>& in, double gain) {
    if (gain == 1.0) {
        return in;
    }
    std::vector<int16_t> out;
    out.reserve(in.size());
    for (int16_t s : in) {
        const int v = static_cast<int>(std::lround(static_cast<double>(s) * gain));
        out.push_back(static_cast<int16_t>(std::clamp(v, -32768, 32767)));
    }
    return out;
}

int64_t CurrentSteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

const std::vector<SignalPattern>& AudioEngine::RogerPatterns() {
    static const std::vector<SignalPattern> kPatterns = {
        {"none", "No signal", {}, true},
        {"variant_1", "Variant 1", {{890.0, 20}, {670.0, 20}, {890.0, 45}, {1000.0, 28}}, true},
        {"variant_2", "Variant 2", {{1000.0, 88}, {800.0, 64}}, true},
        {"variant_3", "Variant 3", {{1330.0, 68}, {1600.0, 56}}, true},
    };
    return kPatterns;
}

const std::vector<SignalPattern>& AudioEngine::CallPatterns() {
    static const std::vector<SignalPattern> kPatterns = [] {
        std::vector<SignalPattern> out;
        SignalPattern p1{"call_variant_1", "Variant 1", {}, false};
        for (int i = 0; i < 9; ++i) {
            p1.points.push_back({2300.0, 70});
            p1.points.push_back({1850.0, 70});
            p1.points.push_back({1450.0, 70});
        }
        out.push_back(std::move(p1));

        SignalPattern p2{"call_variant_2", "Variant 2", {}, false};
        for (int i = 0; i < 14; ++i) {
            p2.points.push_back({1150.0, 35});
            p2.points.push_back({1350.0, 35});
            p2.points.push_back({1550.0, 35});
            p2.points.push_back({1750.0, 35});
            p2.points.push_back({1550.0, 35});
            p2.points.push_back({1350.0, 35});
        }
        out.push_back(std::move(p2));

        SignalPattern p3{"call_variant_3", "Variant 3", {}, false};
        for (int i = 0; i < 32; ++i) {
            p3.points.push_back({2000.0, 60});
            p3.points.push_back({1000.0, 60});
        }
        out.push_back(std::move(p3));
        return out;
    }();
    return kPatterns;
}

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() { Shutdown(); }

bool AudioEngine::Initialize() {
    std::lock_guard<std::mutex> lg(mu_);
    if (!contextReady_) {
        if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
            if (onStatus_) {
                onStatus_("miniaudio: context init failed");
            }
            return false;
        }
        contextReady_ = true;
    }
    RecreateCodecUnlocked();
    if (onStatus_) {
        onStatus_("Audio initialized (miniaudio)");
    }
    return true;
}

void AudioEngine::Shutdown() {
    StopTransmit();

    std::lock_guard<std::mutex> lg(mu_);
    CloseOutputStreamLocked();
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
    if (contextReady_) {
        ma_context_uninit(&context_);
        contextReady_ = false;
    }
}

void AudioEngine::Reconfigure(const WelcomeConfig& cfg) {
    std::lock_guard<std::mutex> lg(mu_);
    if (captureActive_) {
        transmitting_.store(false);
        ma_device_stop(&captureDev_);
        ma_device_uninit(&captureDev_);
        captureActive_ = false;
        captureFifo_.clear();
    }
    const bool timingChanged = cfg.sampleRate != sampleRate_ || cfg.packetMs != packetMs_;
    sampleRate_ = cfg.sampleRate;
    packetMs_ = cfg.packetMs;
    bitrate_ = cfg.bitrate;
    complexity_ = cfg.complexity;
    fec_ = cfg.fec;
    dtx_ = cfg.dtx;
    if (timingChanged) {
        CloseOutputStreamLocked();
    }
    RecreateCodecUnlocked();
}

std::vector<NamedAudioDevice> AudioEngine::ListInputDevices() {
    std::vector<NamedAudioDevice> out;
    ma_context ctx{};
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
        return out;
    }
    ma_device_info* caps = nullptr;
    ma_uint32 nCap = 0;
    ma_context_get_devices(&ctx, nullptr, nullptr, &caps, &nCap);
    for (ma_uint32 i = 0; i < nCap; ++i) {
        out.push_back({static_cast<int>(i), caps[i].name});
    }
    ma_context_uninit(&ctx);
    return out;
}

std::vector<NamedAudioDevice> AudioEngine::ListOutputDevices() {
    std::vector<NamedAudioDevice> out;
    ma_context ctx{};
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
        return out;
    }
    ma_device_info* play = nullptr;
    ma_uint32 nPlay = 0;
    ma_context_get_devices(&ctx, &play, &nPlay, nullptr, nullptr);
    for (ma_uint32 i = 0; i < nPlay; ++i) {
        out.push_back({static_cast<int>(i), play[i].name});
    }
    ma_context_uninit(&ctx);
    return out;
}

void AudioEngine::SetPreferredInputDevice(int indexOrMinusOneForDefault) {
    std::lock_guard<std::mutex> lg(mu_);
    preferredInputDevice_ = indexOrMinusOneForDefault;
}

void AudioEngine::SetPreferredOutputDevice(int indexOrMinusOneForDefault) {
    std::lock_guard<std::mutex> lg(mu_);
    if (preferredOutputDevice_ != indexOrMinusOneForDefault) {
        preferredOutputDevice_ = indexOrMinusOneForDefault;
        CloseOutputStreamLocked();
    }
}

void AudioEngine::SetRogerPatternId(std::string patternId) {
    std::lock_guard<std::mutex> lg(mu_);
    rogerPatternId_ = std::move(patternId);
}

void AudioEngine::SetCallPatternId(std::string patternId) {
    std::lock_guard<std::mutex> lg(mu_);
    callPatternId_ = std::move(patternId);
}

std::string AudioEngine::RogerPatternId() const {
    std::lock_guard<std::mutex> lg(mu_);
    return rogerPatternId_;
}

std::string AudioEngine::CallPatternId() const {
    std::lock_guard<std::mutex> lg(mu_);
    return callPatternId_;
}

int AudioEngine::ResolveInputDeviceIndex() const {
    return preferredInputDevice_;
}

int AudioEngine::ResolveOutputDeviceIndex() const {
    return preferredOutputDevice_;
}

void AudioEngine::CloseOutputStreamLocked() {
    if (playbackActive_) {
        ma_device_stop(&playbackDev_);
        ma_device_uninit(&playbackDev_);
        playbackActive_ = false;
    }
    if (playbackRbReady_) {
        ma_pcm_rb_uninit(&playbackRb_);
        playbackRbReady_ = false;
    }
}

void AudioEngine::CloseCaptureDeviceLocked() {
    if (captureActive_) {
        ma_device_stop(&captureDev_);
        ma_device_uninit(&captureDev_);
        captureActive_ = false;
    }
    captureFifo_.clear();
}

const SignalPattern* AudioEngine::FindRogerPatternLocked() const {
    for (const auto& p : RogerPatterns()) {
        if (p.id == rogerPatternId_) {
            return &p;
        }
    }
    for (const auto& p : customRogerPatterns_) {
        if (p.id == rogerPatternId_) {
            return &p;
        }
    }
    return &RogerPatterns().front();
}

const SignalPattern* AudioEngine::FindCallPatternLocked() const {
    for (const auto& p : CallPatterns()) {
        if (p.id == callPatternId_) {
            return &p;
        }
    }
    for (const auto& p : customCallPatterns_) {
        if (p.id == callPatternId_) {
            return &p;
        }
    }
    return &CallPatterns().front();
}

void AudioEngine::SetCustomSignalPatterns(std::vector<SignalPattern> roger, std::vector<SignalPattern> call) {
    std::lock_guard<std::mutex> lg(mu_);
    customRogerPatterns_ = std::move(roger);
    customCallPatterns_ = std::move(call);
}

void AudioEngine::PlaySignalPatternPreview(const SignalPattern& pattern) {
    if (pattern.points.empty()) {
        return;
    }
    const auto pcm = GenerateSignalPcm(kLocalSignalSynthesisRate, pattern);
    if (pcm.empty()) {
        return;
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

std::vector<int16_t> AudioEngine::GenerateSignalPcm(int sampleRate, const SignalPattern& pattern) {
    if (sampleRate <= 0 || pattern.points.empty()) {
        return {};
    }
    std::vector<int16_t> out;
    out.reserve(static_cast<size_t>(sampleRate));
    constexpr double kPi = 3.14159265358979323846;
    double phase = 0.0;
    for (const auto& seg : pattern.points) {
        const int n = std::max((sampleRate * seg.durationMs) / 1000, 1);
        const bool pause = seg.freqHz <= 0.0;
        const double step = pause ? 0.0 : (2.0 * kPi * seg.freqHz / sampleRate);
        for (int i = 0; i < n; ++i) {
            const double envPos = static_cast<double>(i) / static_cast<double>(n);
            const double env = (envPos < 0.08) ? (envPos / 0.08) : ((envPos > 0.92) ? ((1.0 - envPos) / 0.08) : 1.0);
            const double s = pause ? 0.0 : std::sin(phase) * env * 0.26;
            const int v = static_cast<int>(std::lround(s * 32767.0));
            out.push_back(static_cast<int16_t>(std::clamp(v, -32768, 32767)));
            phase += step;
        }
    }
    if (pattern.appendTail) {
        AppendSilence(out, sampleRate, kRogerTailMs);
    }
    return out;
}

void AudioEngine::RecreateCodecUnlocked() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }

    int err = OPUS_OK;
    encoder_ = opus_encoder_create(sampleRate_, 1, OPUS_APPLICATION_VOIP, &err);
    if (err == OPUS_OK && encoder_) {
        opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_));
        opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(complexity_));
        opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(fec_ ? 1 : 0));
        opus_encoder_ctl(encoder_, OPUS_SET_DTX(dtx_ ? 1 : 0));
    }
    decoder_ = opus_decoder_create(sampleRate_, 1, &err);
}

int AudioEngine::FrameSamples() const {
    return (sampleRate_ * packetMs_) / 1000;
}

void AudioEngine::EnsurePlaybackDeviceLocked() {
    if (playbackActive_ && playbackRbReady_) {
        return;
    }
    CloseOutputStreamLocked();

    ma_device_info* play = nullptr;
    ma_uint32 nPlay = 0;
    ma_context_get_devices(&context_, &play, &nPlay, nullptr, nullptr);
    const ma_device_id* playId = nullptr;
    const int want = ResolveOutputDeviceIndex();
    if (want >= 0 && (ma_uint32)want < nPlay) {
        playId = &play[want].id;
    }

    const ma_uint32 rbFrames = static_cast<ma_uint32>(std::max(FrameSamples() * 64, 1024));
    if (ma_pcm_rb_init(ma_format_s16, 1, rbFrames, nullptr, nullptr, &playbackRb_) != MA_SUCCESS) {
        return;
    }
    playbackRbReady_ = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = playId;
    cfg.playback.format = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate = static_cast<ma_uint32>(sampleRate_);
    cfg.dataCallback = PlaybackCallback;
    cfg.pUserData = this;
    cfg.periodSizeInFrames = static_cast<ma_uint32>(FrameSamples());

    if (ma_device_init(&context_, &cfg, &playbackDev_) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&playbackRb_);
        playbackRbReady_ = false;
        return;
    }
    if (ma_device_start(&playbackDev_) != MA_SUCCESS) {
        ma_device_uninit(&playbackDev_);
        ma_pcm_rb_uninit(&playbackRb_);
        playbackRbReady_ = false;
        return;
    }
    playbackActive_ = true;
}

bool AudioEngine::StartTransmit() {
    std::lock_guard<std::mutex> lg(mu_);
    if (captureActive_ || signalStreaming_.load() || !encoder_ || !contextReady_) {
        return false;
    }

    ma_device_info* caps = nullptr;
    ma_uint32 nCap = 0;
    ma_context_get_devices(&context_, nullptr, nullptr, &caps, &nCap);
    const ma_device_id* capId = nullptr;
    const int wantIn = ResolveInputDeviceIndex();
    if (wantIn >= 0 && (ma_uint32)wantIn < nCap) {
        capId = &caps[wantIn].id;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.pDeviceID = capId;
    cfg.capture.format = ma_format_s16;
    cfg.capture.channels = 1;
    cfg.sampleRate = static_cast<ma_uint32>(sampleRate_);
    cfg.dataCallback = CaptureCallback;
    cfg.pUserData = this;
    cfg.periodSizeInFrames = static_cast<ma_uint32>(FrameSamples());

    if (ma_device_init(&context_, &cfg, &captureDev_) != MA_SUCCESS) {
        return false;
    }

    captureFifo_.clear();
    opusScratch_.resize(1275);
    transmitting_.store(true);
    if (ma_device_start(&captureDev_) != MA_SUCCESS) {
        transmitting_.store(false);
        ma_device_uninit(&captureDev_);
        return false;
    }
    captureActive_ = true;
    return true;
}

void AudioEngine::StopTransmit() {
    transmitting_.store(false);
    std::lock_guard<std::mutex> lg(mu_);
    CloseCaptureDeviceLocked();
}

void AudioEngine::ScheduleRxResumeHoldoff(int multiplier) {
    const int safeMult = std::max(multiplier, 1);
    int packetMs = 20;
    {
        std::lock_guard<std::mutex> lg(mu_);
        packetMs = std::max(packetMs_, 10);
    }
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    rxResumeAtNs_.store(nowNs + static_cast<int64_t>(packetMs) * safeMult * 1'000'000LL);
}

bool AudioEngine::IsRxHoldoffActive() const {
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    return nowNs < rxResumeAtNs_.load();
}

bool AudioEngine::StreamRogerSignal() {
    if (transmitting_.load() || signalStreaming_.exchange(true)) {
        return false;
    }
    std::vector<int16_t> remotePcm;
    std::vector<int16_t> localPcm;
    int streamRate = 8000;
    {
        std::lock_guard<std::mutex> lg(mu_);
        streamRate = sampleRate_;
        const SignalPattern* pattern = FindRogerPatternLocked();
        if (!pattern || pattern->points.empty()) {
            signalStreaming_.store(false);
            return false;
        }
        const auto localRoger = GenerateSignalPcm(kLocalSignalSynthesisRate, *pattern);
        remotePcm = ResampleLinear(localRoger, kLocalSignalSynthesisRate, streamRate);
        std::vector<int16_t> pttRelease = LoadSoundFromExeDir("selfttdown_002", kLocalSignalSynthesisRate);
        if (pttRelease.empty()) {
            AppendTone(pttRelease, kLocalSignalSynthesisRate, 760.0, 28, 0.18);
            AppendSilence(pttRelease, kLocalSignalSynthesisRate, 18);
        }
        localPcm.reserve(pttRelease.size() + localRoger.size());
        localPcm.insert(localPcm.end(), pttRelease.begin(), pttRelease.end());
        auto rogerLocal = ApplyGain(localRoger, 0.9);
        localPcm.insert(localPcm.end(), rogerLocal.begin(), rogerLocal.end());
    }
    PlayOneShotHighQuality(localPcm, kLocalSignalSynthesisRate);
    const bool ok = StreamGeneratedSignal(remotePcm);
    ScheduleRxResumeHoldoff();
    signalStreaming_.store(false);
    return ok;
}

bool AudioEngine::StreamCallSignal() {
    if (transmitting_.load() || signalStreaming_.exchange(true)) {
        return false;
    }
    std::vector<int16_t> remotePcm;
    std::vector<int16_t> localPcm;
    int streamRate = 8000;
    {
        std::lock_guard<std::mutex> lg(mu_);
        streamRate = sampleRate_;
        const SignalPattern* pattern = FindCallPatternLocked();
        if (!pattern || pattern->points.empty()) {
            signalStreaming_.store(false);
            return false;
        }
        localPcm = GenerateSignalPcm(kLocalSignalSynthesisRate, *pattern);
        remotePcm = ResampleLinear(localPcm, kLocalSignalSynthesisRate, streamRate);
        localPcm = ApplyGain(localPcm, 0.55);
    }
    PlayOneShotHighQuality(localPcm, kLocalSignalSynthesisRate);
    const bool ok = StreamGeneratedSignal(remotePcm);
    ScheduleRxResumeHoldoff();
    signalStreaming_.store(false);
    return ok;
}

void AudioEngine::PlayConnectedSignal() {
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lg(mu_);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1400.0, 40, 0.22);
        AppendSilence(pcm, kLocalSignalSynthesisRate, 50);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1700.0, 50, 0.22);
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

void AudioEngine::PlayConnectionErrorSignal() {
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lg(mu_);
        AppendTone(pcm, kLocalSignalSynthesisRate, 890.0, 192, 0.2);
        AppendSilence(pcm, kLocalSignalSynthesisRate, 300);
        AppendTone(pcm, kLocalSignalSynthesisRate, 890.0, 200, 0.2);
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

void AudioEngine::PlayManualConnectStartSignal() {
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lg(mu_);
        AppendTone(pcm, kLocalSignalSynthesisRate, 932.33, 50, 0.22);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1174.66, 50, 0.22);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1396.91, 50, 0.22);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1864.66, 70, 0.22);
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

void AudioEngine::PlayManualDisconnectSignal() {
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lg(mu_);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1864.66, 70, 0.22);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1396.91, 50, 0.22);
        AppendTone(pcm, kLocalSignalSynthesisRate, 1174.66, 50, 0.22);
        AppendTone(pcm, kLocalSignalSynthesisRate, 932.33, 50, 0.22);
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

void AudioEngine::PlayPttPressSignal() {
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lg(mu_);
        pcm = LoadSoundFromExeDir("selfpttup_002", kLocalSignalSynthesisRate);
        if (pcm.empty()) {
            AppendTone(pcm, kLocalSignalSynthesisRate, 1260.0, 24, 0.18);
            AppendSilence(pcm, kLocalSignalSynthesisRate, 10);
        }
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

void AudioEngine::PlayVibrationPattern(const std::vector<int>& patternMs) {
    if (patternMs.empty()) {
        return;
    }
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lg(mu_);
        bool toneOn = true;
        for (int d : patternMs) {
            if (d <= 0) {
                continue;
            }
            if (toneOn) {
                AppendTone(pcm, kLocalSignalSynthesisRate, 100.0, d, 0.18);
            } else {
                AppendSilence(pcm, kLocalSignalSynthesisRate, d);
            }
            toneOn = !toneOn;
        }
    }
    PlayOneShotHighQuality(pcm, kLocalSignalSynthesisRate);
}

void AudioEngine::QueuePcmForPlaybackLocked(const std::vector<int16_t>& pcm) {
    if (pcm.empty()) {
        return;
    }
    EnsurePlaybackDeviceLocked();
    if (!playbackRbReady_ || !playbackActive_) {
        return;
    }
    ma_uint32 toWrite = static_cast<ma_uint32>(pcm.size());
    const int16_t* pSrc = pcm.data();
    while (toWrite > 0) {
        ma_uint32 chunk = toWrite;
        void* pW = nullptr;
        ma_pcm_rb_acquire_write(&playbackRb_, &chunk, &pW);
        if (chunk == 0) {
            break;
        }
        std::memcpy(pW, pSrc, chunk * sizeof(int16_t));
        ma_pcm_rb_commit_write(&playbackRb_, chunk);
        pSrc += chunk;
        toWrite -= chunk;
    }
}

bool AudioEngine::StreamGeneratedSignal(const std::vector<int16_t>& pcmSignal) {
    if (pcmSignal.empty()) {
        return false;
    }

    int frameSamples = 0;
    int frameMs = 0;
    {
        std::lock_guard<std::mutex> lg(mu_);
        if (!encoder_ || !contextReady_) {
            return false;
        }
        frameSamples = FrameSamples();
        frameMs = packetMs_;
    }
    if (frameSamples <= 0 || frameMs <= 0) {
        return false;
    }

    const int64_t frameNs = static_cast<int64_t>(frameMs) * 1'000'000LL;
    int64_t nextFrameAtNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();

    std::vector<uint8_t> opusBuf(1275);
    std::vector<int16_t> frame(static_cast<size_t>(frameSamples), 0);
    size_t offset = 0;
    while (offset < pcmSignal.size()) {
        std::fill(frame.begin(), frame.end(), 0);
        const size_t copyN = std::min(frame.size(), pcmSignal.size() - offset);
        std::memcpy(frame.data(), pcmSignal.data() + offset, copyN * sizeof(int16_t));
        offset += copyN;

        int encoded = 0;
        {
            std::lock_guard<std::mutex> lg(mu_);
            if (!encoder_) {
                return false;
            }
            encoded = opus_encode(encoder_, frame.data(), frameSamples, opusBuf.data(), static_cast<int>(opusBuf.size()));
        }
        if (encoded > 0 && onEncodedFrame_) {
            onEncodedFrame_(opusBuf.data(), static_cast<size_t>(encoded), 255);
        }
        if (offset < pcmSignal.size()) {
            nextFrameAtNs += frameNs;
            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
            const int64_t sleepNs = nextFrameAtNs - nowNs;
            if (sleepNs > 0) {
                const int64_t sleepMs = std::max<int64_t>(1, sleepNs / 1'000'000LL);
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            } else if (sleepNs < -frameNs * 2) {
                // If scheduling drift grew too much, re-anchor pacing from current time.
                nextFrameAtNs = nowNs;
            }
        }
    }
    return true;
}

void AudioEngine::OnCaptureFrames(const void* pInput, ma_uint32 frameCount) {
    if (!transmitting_.load() || !pInput || frameCount == 0) {
        return;
    }
    const auto* s = static_cast<const int16_t*>(pInput);
    captureFifo_.insert(captureFifo_.end(), s, s + frameCount);
    DrainCaptureFifoOpus();
}

void AudioEngine::DrainCaptureFifoOpus() {
    OpusEncoder* enc = encoder_;
    if (!enc) {
        return;
    }
    const int frame = FrameSamples();
    if (frame <= 0) {
        return;
    }
    while (static_cast<int>(captureFifo_.size()) >= frame) {
        int peak = 0;
        for (int i = 0; i < frame; ++i) {
            peak = std::max(peak, std::abs(static_cast<int>(captureFifo_[i])));
        }
        const int level = std::clamp((peak * 100) / 32767, 0, 100);
        if (onLevel_) {
            onLevel_(level);
        }

        const int bytes = opus_encode(enc, captureFifo_.data(), frame, opusScratch_.data(), static_cast<int>(opusScratch_.size()));
        captureFifo_.erase(captureFifo_.begin(), captureFifo_.begin() + frame);
        if (bytes > 0 && onEncodedFrame_) {
            onEncodedFrame_(opusScratch_.data(), static_cast<size_t>(bytes), 255);
        }
    }
}

void AudioEngine::SetRxVolumePercent(int percent) {
    const int p = std::clamp(percent, kRxVolumeMinPercent, kRxVolumeMaxPercent);
    rxVolumePercent_.store(p, std::memory_order_relaxed);
}

void AudioEngine::OnIncomingOpusFrame(const std::vector<uint8_t>& opus) {
    if (opus.empty()) {
        return;
    }
    const int64_t now = CurrentSteadyNs();
    const int64_t prevInbound = lastInboundNs_.exchange(now);
    if (transmitting_.load() || signalStreaming_.load()) {
        const bool isNewBurst = prevInbound == 0 || (now - prevInbound) > 600'000'000LL;
        const int64_t lastBuzz = lastTxCollisionToneNs_.load();
        const bool cooldownOk = lastBuzz == 0 || (now - lastBuzz) > 1'500'000'000LL;
        if (isNewBurst && cooldownOk) {
            lastTxCollisionToneNs_.store(now);
            // Android-like "vibration" pattern emulation for desktop.
            PlayVibrationPattern({35, 55, 35, 55, 35});
        }
        return;
    }

    std::lock_guard<std::mutex> lg(mu_);
    if (!decoder_ || IsRxHoldoffActive()) {
        return;
    }

    EnsurePlaybackDeviceLocked();
    if (!playbackRbReady_ || !playbackActive_) {
        return;
    }

    const int frame = FrameSamples();
    std::vector<int16_t> pcm(static_cast<size_t>(frame));
    const int decoded = opus_decode(decoder_, opus.data(), static_cast<int>(opus.size()), pcm.data(), frame, 0);
    if (decoded <= 0) {
        return;
    }

    const int pct = rxVolumePercent_.load(std::memory_order_relaxed);
    if (pct != kRxVolumeDefaultPercent) {
        const double gain = static_cast<double>(pct) / 100.0;
        for (auto& s : pcm) {
            const int v = static_cast<int>(std::lround(static_cast<double>(s) * gain));
            s = static_cast<int16_t>(std::clamp(v, -32768, 32767));
        }
    }

    QueuePcmForPlaybackLocked(pcm);
}
