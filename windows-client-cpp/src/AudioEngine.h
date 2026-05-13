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
    /** Calling signals: repeat base `points` this many times when synthesizing (Roger uses 1). */
    int repeatCount = 1;
};

class AudioEngine {
public:
    using EncodedFrameCallback = std::function<void(const uint8_t* data, size_t size, uint8_t signal)>;
    using StatusCallback = std::function<void(const std::string&)>;
    using LevelCallback = std::function<void(int)>;
    using ParallelTxCollisionCallback = std::function<void(bool)>;

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
    /// Desktop "vibration" imitation (sine bursts): frequency (Hz) and volume 0–100 (%).
    /// Used for parallel-TX feedback, transmit time-limit countdown, and similar cues.
    void SetVibrationImitation(double freqHz, int volumePercent);
    /// Preview using the given parameters without changing stored settings.
    void PlayVibrationImitationPreview(double freqHz, int volumePercent);
    /// One short sine burst at current imitation settings (duration in ms).
    void PlayVibrationImitationPulse(int durationMs);
    void SetRogerPatternId(std::string patternId);
    void SetCallPatternId(std::string patternId);
    std::string RogerPatternId() const;
    std::string CallPatternId() const;

    bool StartTransmit();
    void StopTransmit();
    /// Best-effort: break out of an in-progress Roger/Call Opus stream loop (no Roger tail).
    void RequestAbortOutgoingSignalStream();
    bool IsTransmitting() const { return transmitting_.load(); }
    bool IsSignalStreaming() const { return signalStreaming_.load(); }
    bool IsParallelTxCollisionActive() const { return parallelTxCollisionActive_.load(std::memory_order_relaxed); }
    void PollParallelTxCollisionState(int64_t steadyNowNs);
    /// After local TX/Roger/Call dropped inbound audio, refresh RX Opus decoder on next decode (no time delay).
    void ScheduleRxResumeHoldoff(int multiplier = 2);
    bool StreamRogerSignal();
    bool StreamCallSignal();
    void PlayConnectedSignal(bool useQueue = true);
    void PlayConnectionErrorSignal();
    void PlayManualConnectStartSignal(bool useQueue = true);
    void PlayManualDisconnectSignal();
    void PlaySwitchNavSignal(bool useQueue = false);
    void PlayRxVolumePreviewSignal(int volumePercent);
    void PlayPttPressSignal();
    /// Local preview (e.g. custom pattern editor); does not touch relay state.
    void PlaySignalPatternPreview(const SignalPattern& pattern);
    void SetCustomSignalPatterns(std::vector<SignalPattern> roger, std::vector<SignalPattern> call);

    void OnIncomingOpusFrame(const std::vector<uint8_t>& opus);

    void SetEncodedFrameCallback(EncodedFrameCallback cb) { onEncodedFrame_ = std::move(cb); }
    void SetStatusCallback(StatusCallback cb) { onStatus_ = std::move(cb); }
    void SetLevelCallback(LevelCallback cb) { onLevel_ = std::move(cb); }
    void SetParallelTxCollisionCallback(ParallelTxCollisionCallback cb) { onParallelTxCollision_ = std::move(cb); }

private:
    static std::vector<int16_t> GenerateSignalPcm(int sampleRate, const SignalPattern& pattern);
    const SignalPattern* FindRogerPatternLocked() const;
    const SignalPattern* FindCallPatternLocked() const;
    void QueuePcmForPlaybackLocked(const std::vector<int16_t>& pcm);
    bool StreamGeneratedSignal(const std::vector<int16_t>& pcmSignal);
    void PlayVibrationImitationPattern(const std::vector<int>& patternMs);
    void RecreateCodecUnlocked();
    void RecreateRxDecoderUnlocked();
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
    std::vector<SignalPattern> customRogerPatterns_;
    std::vector<SignalPattern> customCallPatterns_;

    std::vector<int16_t> captureFifo_;
    std::vector<uint8_t> opusScratch_;

    std::atomic<bool> transmitting_{false};
    std::atomic<bool> signalStreaming_{false};
    std::atomic<bool> signalStreamAbortRequested_{false};
    /// After dropping inbound audio during local TX / signal stream, rebuild RX decoder before next decode.
    std::atomic<bool> refresh_rx_decoder_{false};
    std::atomic<int64_t> lastInboundNs_{0};
    /// Inbound audio while local TX / signal stream is active (another station doubling).
    std::atomic<int64_t> lastRxDuringLocalTxNs_{0};
    std::atomic<int64_t> lastParallelCollisionPulseNs_{0};
    std::atomic<bool> parallelTxCollisionActive_{false};
    static constexpr int64_t kParallelRxDuringTxStaleNs = 250'000'000LL;
    static constexpr int64_t kParallelPulseMinGapNs = 55'000'000LL;
    static constexpr int kParallelCollisionPulseMs = 24;
    std::atomic<int> rxVolumePercent_{100};

    double vibrationImitationHz_{100.0};
    int vibrationImitationVolumePercent_{40};

    EncodedFrameCallback onEncodedFrame_;
    StatusCallback onStatus_;
    LevelCallback onLevel_;
    ParallelTxCollisionCallback onParallelTxCollision_;
};
