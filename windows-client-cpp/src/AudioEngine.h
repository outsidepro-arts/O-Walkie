#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "miniaudio.h"

struct OpusEncoder;
struct OpusDecoder;
struct WelcomeConfig;

struct NamedAudioDevice {
    int index = -1;
    std::string name;
};

struct SignalPatternPoint {
    double freqHz = 0.0;
    int durationMs = 0;
};

struct SignalPattern {
    std::string id;
    std::string name;
    std::vector<SignalPatternPoint> points;
    bool appendTail = false;
};

class AudioEngine {
public:
    using EncodedFrameCallback = std::function<void(const uint8_t* data, size_t size, uint8_t signal)>;
    using StatusCallback = std::function<void(const std::string&)>;
    using LevelCallback = std::function<void(int)>;

    AudioEngine();
    ~AudioEngine();

    bool Initialize();
    void Shutdown();
    void Reconfigure(const WelcomeConfig& cfg);

    static std::vector<NamedAudioDevice> ListInputDevices();
    static std::vector<NamedAudioDevice> ListOutputDevices();
    static const std::vector<SignalPattern>& RogerPatterns();
    static const std::vector<SignalPattern>& CallPatterns();

    void SetPreferredInputDevice(int indexOrMinusOneForDefault);
    void SetPreferredOutputDevice(int indexOrMinusOneForDefault);
    /// Incoming (RX) stream gain, 0–200 (%), same semantics as Android `RxVolumeStore`.
    void SetRxVolumePercent(int percent);
    void SetRogerPatternId(std::string patternId);
    void SetCallPatternId(std::string patternId);
    std::string RogerPatternId() const;
    std::string CallPatternId() const;

    bool StartTransmit();
    void StopTransmit();
    bool IsTransmitting() const { return transmitting_.load(); }
    bool IsSignalStreaming() const { return signalStreaming_.load(); }
    void ScheduleRxResumeHoldoff(int multiplier = 2);
    bool StreamRogerSignal();
    bool StreamCallSignal();
    void PlayConnectedSignal();
    void PlayConnectionErrorSignal();
    void PlayManualConnectStartSignal();
    void PlayManualDisconnectSignal();
    void PlayPttPressSignal();

    void OnIncomingOpusFrame(const std::vector<uint8_t>& opus);

    void SetEncodedFrameCallback(EncodedFrameCallback cb) { onEncodedFrame_ = std::move(cb); }
    void SetStatusCallback(StatusCallback cb) { onStatus_ = std::move(cb); }
    void SetLevelCallback(LevelCallback cb) { onLevel_ = std::move(cb); }

private:
    static std::vector<int16_t> GenerateSignalPcm(int sampleRate, const SignalPattern& pattern);
    const SignalPattern* FindRogerPatternLocked() const;
    const SignalPattern* FindCallPatternLocked() const;
    void QueuePcmForPlaybackLocked(const std::vector<int16_t>& pcm);
    bool IsRxHoldoffActive() const;
    bool StreamGeneratedSignal(const std::vector<int16_t>& pcmSignal);
    void PlayVibrationPattern(const std::vector<int>& patternMs);
    void RecreateCodecUnlocked();
    int FrameSamples() const;
    int ResolveInputDeviceIndex() const;
    int ResolveOutputDeviceIndex() const;
    void CloseOutputStreamLocked();
    void CloseCaptureDeviceLocked();
    void EnsurePlaybackDeviceLocked();

    static void CaptureCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void PlaybackCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void OnCaptureFrames(const void* pInput, ma_uint32 frameCount);
    void DrainCaptureFifoOpus();

private:
    mutable std::mutex mu_;
    ma_context context_{};
    bool contextReady_ = false;

    ma_device captureDev_{};
    bool captureActive_ = false;

    ma_device playbackDev_{};
    bool playbackActive_ = false;

    ma_pcm_rb playbackRb_{};
    bool playbackRbReady_ = false;

    OpusEncoder* encoder_ = nullptr;
    OpusDecoder* decoder_ = nullptr;

    int sampleRate_ = 8000;
    int packetMs_ = 20;
    int bitrate_ = 12000;
    int complexity_ = 5;
    bool fec_ = true;
    bool dtx_ = false;

    int preferredInputDevice_ = -1;
    int preferredOutputDevice_ = -1;
    std::string rogerPatternId_ = "variant_1";
    std::string callPatternId_ = "call_variant_1";

    std::vector<int16_t> captureFifo_;
    std::vector<uint8_t> opusScratch_;

    std::atomic<bool> transmitting_{false};
    std::atomic<bool> signalStreaming_{false};
    std::atomic<int64_t> rxResumeAtNs_{0};
    std::atomic<int64_t> lastInboundNs_{0};
    std::atomic<int64_t> lastTxCollisionToneNs_{0};
    std::atomic<int> rxVolumePercent_{100};

    EncodedFrameCallback onEncodedFrame_;
    StatusCallback onStatus_;
    LevelCallback onLevel_;
};
