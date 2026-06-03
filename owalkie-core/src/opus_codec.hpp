#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "owalkie/types.hpp"

namespace owalkie::codec {

class OpusCodec {
public:
    OpusCodec();
    ~OpusCodec();

    OpusCodec(const OpusCodec&) = delete;
    OpusCodec& operator=(const OpusCodec&) = delete;

    Result configure(const WelcomeConfig& config);
    void reset();

    Result encode(std::span<const int16_t> pcm, std::vector<uint8_t>& out);
    Result decode(std::span<const uint8_t> opus, int frameSamples, std::vector<int16_t>& out);

    int frameSamples() const { return frameSamples_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    int sampleRate_ = 8000;
    int frameSamples_ = 0;
};

} // namespace owalkie::codec
