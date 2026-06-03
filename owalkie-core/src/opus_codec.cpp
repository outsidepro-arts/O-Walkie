#include "opus_codec.hpp"

#include <opus/opus.h>

#include "owalkie/protocol.hpp"

namespace owalkie::codec {
namespace {

int mapApplication(const std::string& app) {
    if (app == "audio") {
        return OPUS_APPLICATION_AUDIO;
    }
    if (app == "lowdelay") {
        return OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    }
    return OPUS_APPLICATION_VOIP;
}

} // namespace

struct OpusCodec::Impl {
    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;
};

OpusCodec::OpusCodec() : impl_(new Impl()) {}

OpusCodec::~OpusCodec() {
    if (impl_) {
        if (impl_->encoder) {
            opus_encoder_destroy(impl_->encoder);
        }
        if (impl_->decoder) {
            opus_decoder_destroy(impl_->decoder);
        }
        delete impl_;
    }
}

Result OpusCodec::configure(const WelcomeConfig& config) {
    reset();
    sampleRate_ = config.sampleRate;
    frameSamples_ = protocol::frameSamples(config.sampleRate, config.packetMs);
    if (frameSamples_ <= 0) {
        return Result::InvalidArg;
    }

    int err = OPUS_OK;
    impl_->encoder = opus_encoder_create(sampleRate_, 1, mapApplication(config.opus.application), &err);
    if (err != OPUS_OK || !impl_->encoder) {
        return Result::Internal;
    }
    opus_encoder_ctl(impl_->encoder, OPUS_SET_BITRATE(config.opus.bitrate));
    opus_encoder_ctl(impl_->encoder, OPUS_SET_COMPLEXITY(config.opus.complexity));
    opus_encoder_ctl(impl_->encoder, OPUS_SET_INBAND_FEC(config.opus.fec ? 1 : 0));
    opus_encoder_ctl(impl_->encoder, OPUS_SET_DTX(config.opus.dtx ? 1 : 0));

    impl_->decoder = opus_decoder_create(sampleRate_, 1, &err);
    if (err != OPUS_OK || !impl_->decoder) {
        return Result::Internal;
    }
    return Result::Ok;
}

void OpusCodec::reset() {
    if (impl_->encoder) {
        opus_encoder_destroy(impl_->encoder);
        impl_->encoder = nullptr;
    }
    if (impl_->decoder) {
        opus_decoder_destroy(impl_->decoder);
        impl_->decoder = nullptr;
    }
    frameSamples_ = 0;
}

Result OpusCodec::encode(std::span<const int16_t> pcm, std::vector<uint8_t>& out) {
    if (!impl_->encoder || static_cast<int>(pcm.size()) != frameSamples_) {
        return Result::InvalidArg;
    }
    out.resize(512);
    const int n = opus_encode(
        impl_->encoder,
        pcm.data(),
        frameSamples_,
        out.data(),
        static_cast<opus_int32>(out.size()));
    if (n < 0) {
        return Result::Internal;
    }
    out.resize(static_cast<size_t>(n));
    return Result::Ok;
}

Result OpusCodec::decode(std::span<const uint8_t> opus, int frameSamples, std::vector<int16_t>& out) {
    if (!impl_->decoder || frameSamples <= 0) {
        return Result::InvalidArg;
    }
    out.assign(static_cast<size_t>(frameSamples), 0);
    const int n = opus_decode(
        impl_->decoder,
        opus.data(),
        static_cast<opus_int32>(opus.size()),
        out.data(),
        frameSamples,
        0);
    if (n < 0) {
        return Result::Internal;
    }
    out.resize(static_cast<size_t>(n));
    return Result::Ok;
}

} // namespace owalkie::codec
