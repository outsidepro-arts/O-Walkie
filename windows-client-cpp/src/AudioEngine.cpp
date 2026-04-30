#include "AudioEngine.h"

#include "RelayClient.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <opus/opus.h>

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
    if (captureActive_ || !encoder_ || !contextReady_) {
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

void AudioEngine::OnIncomingOpusFrame(const std::vector<uint8_t>& opus) {
    std::lock_guard<std::mutex> lg(mu_);
    if (!decoder_ || opus.empty() || transmitting_.load()) {
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

    ma_uint32 toWrite = static_cast<ma_uint32>(decoded);
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
