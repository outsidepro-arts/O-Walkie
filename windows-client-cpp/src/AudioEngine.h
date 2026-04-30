#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

typedef void PaStream;
struct OpusEncoder;
struct OpusDecoder;
struct WelcomeConfig;

struct NamedAudioDevice {
    int index = -1;
    std::string name;
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

    void SetPreferredInputDevice(int indexOrMinusOneForDefault);
    void SetPreferredOutputDevice(int indexOrMinusOneForDefault);

    bool StartTransmit();
    void StopTransmit();
    bool IsTransmitting() const { return transmitting_.load(); }

    void OnIncomingOpusFrame(const std::vector<uint8_t>& opus);

    void SetEncodedFrameCallback(EncodedFrameCallback cb) { onEncodedFrame_ = std::move(cb); }
    void SetStatusCallback(StatusCallback cb) { onStatus_ = std::move(cb); }
    void SetLevelCallback(LevelCallback cb) { onLevel_ = std::move(cb); }

private:
    void RecreateCodec();
    int FrameSamples() const;
    int ResolveInputDevice() const;
    int ResolveOutputDevice() const;
    void CloseOutputStreamLocked();

private:
    mutable std::mutex mu_;
    PaStream* inputStream_ = nullptr;
    PaStream* outputStream_ = nullptr;
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

    std::atomic<bool> transmitting_{false};

    EncodedFrameCallback onEncodedFrame_;
    StatusCallback onStatus_;
    LevelCallback onLevel_;
};
