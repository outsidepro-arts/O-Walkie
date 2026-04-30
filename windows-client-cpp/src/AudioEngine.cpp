#include "AudioEngine.h"

#include "RelayClient.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
    if (outputStream_) {
        Pa_StopStream(outputStream_);
        Pa_CloseStream(outputStream_);
        outputStream_ = nullptr;
    }
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
    sampleRate_ = cfg.sampleRate;
    packetMs_ = cfg.packetMs;
    bitrate_ = cfg.bitrate;
    complexity_ = cfg.complexity;
    fec_ = cfg.fec;
    dtx_ = cfg.dtx;
    RecreateCodec();
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
    inParams.device = Pa_GetDefaultInputDevice();
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
        outParams.device = Pa_GetDefaultOutputDevice();
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
