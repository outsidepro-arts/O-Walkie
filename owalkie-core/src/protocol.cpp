#include "owalkie/protocol.hpp"

namespace owalkie::protocol {

int normalizeSampleRate(int value) {
    switch (value) {
        case 8000:
        case 12000:
        case 16000:
        case 24000:
        case 48000:
            return value;
        default:
            return 8000;
    }
}

int normalizePacketMs(int value) {
    switch (value) {
        case 10:
        case 20:
        case 40:
        case 60:
            return value;
        default:
            return 20;
    }
}

int frameSamples(int sampleRate, int packetMs) {
    if (sampleRate <= 0 || packetMs <= 0) {
        return 0;
    }
    return (sampleRate * packetMs) / 1000;
}

} // namespace owalkie::protocol
