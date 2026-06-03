#pragma once

#include <cstdint>
#include <vector>

#include "owalkie/types.hpp"

namespace owalkie::signal {

std::vector<int16_t> generatePcm(const SignalPattern& pattern, int sampleRateHz);

} // namespace owalkie::signal
