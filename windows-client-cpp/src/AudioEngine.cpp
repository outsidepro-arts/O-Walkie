#include "AudioEngine.h"

#include "RelayClient.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

#include <opus/opus.h>
#include <portaudio.h>

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() { Shutdown(); }

bool AudioEngine::Initialize() {
    std::lock_guard<std::mutex> lg(mu_);
    if (Pa_Initialize() != paNoError) {
        if (onStatus_) onStatus_("PortAudio init failed");
        return false;
    }
    RecreateCodec();
    if (onStatus_) onStatus_("Audio initialized");
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
    Pa_Terminate();
}

void AudioEngine::Reconfigure(const WelcomeConfig& cfg) {
    std::lock_guard<std::mutex> lg(mu_);
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
    RecreateCodec();
}

std::vector<NamedAudioDevice> AudioEngine::ListInputDevices() {
    std::vector<NamedAudioDevice> out;
    int n = Pa_GetDeviceCount();
    if (n < 0) {
        return out;
    }
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels < 1) {
            continue;
        }
        out.push_back({i, info->name ? std::string(info->name) : std::string("Input ") + std::to_string(i)});
    }
    return out;
}

std::vector<NamedAudioDevice> AudioEngine::ListOutputDevices() {
    std::vector<NamedAudioDevice> out;
    int n = Pa_GetDeviceCount();
    if (n < 0) {
        return out;
    }
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels < 1) {
            continue;
        }
        out.push_back({i, info->name ? std::string(info->name) : std::string("Output ") + std::to_string(i)});
    }
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

int AudioEngine::ResolveInputDevice() const {
    if (preferredInputDevice_ < 0) {
        return Pa_GetDefaultInputDevice();
    }
    if (preferredInputDevice_ >= Pa_GetDeviceCount()) {
        return Pa_GetDefaultInputDevice();
    }
    const PaDeviceInfo* info = Pa_GetDeviceInfo(preferredInputDevice_);
    if (!info || info->maxInputChannels < 1) {
        return Pa_GetDefaultInputDevice();
    }
    return preferredInputDevice_;
}

int AudioEngine::ResolveOutputDevice() const {
    if (preferredOutputDevice_ < 0) {
        return Pa_GetDefaultOutputDevice();
    }
    if (preferredOutputDevice_ >= Pa_GetDeviceCount()) {
        return Pa_GetDefaultOutputDevice();
    }
    const PaDeviceInfo* info = Pa_GetDeviceInfo(preferredOutputDevice_);
    if (!info || info->maxOutputChannels < 1) {
        return Pa_GetDefaultOutputDevice();
    }
    return preferredOutputDevice_;
}

void AudioEngine::CloseOutputStreamLocked() {
    if (outputStream_) {
        Pa_StopStream(outputStream_);
        Pa_CloseStream(outputStream_);
        outputStream_ = nullptr;
    }
}

int AudioEngine::FrameSamples() const {
    return (sampleRate_ * packetMs_) / 1000;
}

void AudioEngine::RecreateCodec() {
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

bool AudioEngine::StartTransmit() {
    std::lock_guard<std::mutex> lg(mu_);
    if (transmitting_.load() || !encoder_) return false;

    PaStreamParameters inParams{};
    inParams.device = ResolveInputDevice();
    if (inParams.device == paNoDevice) return false;
    inParams.channelCount = 1;
    inParams.sampleFormat = paInt16;
    inParams.suggestedLatency = Pa_GetDeviceInfo(inParams.device)->defaultLowInputLatency;

    auto err = Pa_OpenStream(
        &inputStream_,
        &inParams,
        nullptr,
        sampleRate_,
        FrameSamples(),
        paNoFlag,
        nullptr,
        nullptr
    );
    if (err != paNoError) return false;
    if (Pa_StartStream(inputStream_) != paNoError) return false;

    transmitting_.store(true);
    std::thread([this] {
        std::vector<int16_t> pcm(FrameSamples(), 0);
        std::vector<uint8_t> opus(1275, 0);
        while (transmitting_.load()) {
            if (Pa_ReadStream(inputStream_, pcm.data(), pcm.size()) != paNoError) continue;

            int peak = 0;
            for (auto s : pcm) peak = std::max(peak, std::abs((int)s));
            int level = std::clamp((peak * 100) / 32767, 0, 100);
            if (onLevel_) onLevel_(level);

            int bytes = opus_encode(encoder_, pcm.data(), (int)pcm.size(), opus.data(), (int)opus.size());
            if (bytes > 0 && onEncodedFrame_) {
                onEncodedFrame_(opus.data(), (size_t)bytes, 255);
            }
        }
    }).detach();
    return true;
}

void AudioEngine::StopTransmit() {
    transmitting_.store(false);
    std::lock_guard<std::mutex> lg(mu_);
    if (inputStream_) {
        Pa_StopStream(inputStream_);
        Pa_CloseStream(inputStream_);
        inputStream_ = nullptr;
    }
}

void AudioEngine::OnIncomingOpusFrame(const std::vector<uint8_t>& opus) {
    std::lock_guard<std::mutex> lg(mu_);
    if (!decoder_ || opus.empty() || transmitting_.load()) return;

    if (!outputStream_) {
        PaStreamParameters outParams{};
        outParams.device = ResolveOutputDevice();
        if (outParams.device == paNoDevice) return;
        outParams.channelCount = 1;
        outParams.sampleFormat = paInt16;
        outParams.suggestedLatency = Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;
        if (Pa_OpenStream(
                &outputStream_,
                nullptr,
                &outParams,
                sampleRate_,
                FrameSamples(),
                paNoFlag,
                nullptr,
                nullptr
            ) != paNoError) {
            outputStream_ = nullptr;
            return;
        }
        if (Pa_StartStream(outputStream_) != paNoError) return;
    }

    std::vector<int16_t> pcm(FrameSamples(), 0);
    int decoded = opus_decode(decoder_, opus.data(), (int)opus.size(), pcm.data(), FrameSamples(), 0);
    if (decoded > 0) {
        Pa_WriteStream(outputStream_, pcm.data(), decoded);
    }
}
