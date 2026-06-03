#pragma once

#include "owalkie/types.hpp"

namespace owalkie::protocol {

int normalizeSampleRate(int value);
int normalizePacketMs(int value);
int frameSamples(int sampleRate, int packetMs);

} // namespace owalkie::protocol
