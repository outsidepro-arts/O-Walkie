#include "owalkie/signal.hpp"

#include <algorithm>
#include <cmath>

namespace owalkie::signal {
namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

std::vector<int16_t> generatePcm(const SignalPattern& pattern, int sampleRateHz) {
    if (sampleRateHz <= 0 || pattern.points.empty()) {
        return {};
    }

    const int reps = std::max(1, pattern.repeatCount);
    std::vector<SignalPoint> segments;
    segments.reserve(pattern.points.size() * static_cast<size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        segments.insert(segments.end(), pattern.points.begin(), pattern.points.end());
    }

    const int tailSamples = (pattern.tailMs > 0) ? (sampleRateHz * pattern.tailMs) / 1000 : 0;
    std::vector<int16_t> out;
    out.reserve(static_cast<size_t>(sampleRateHz + tailSamples));

    double phase = 0.0;
    const double amplitude = pattern.gain;

    for (const auto& seg : segments) {
        const int n = std::max((sampleRateHz * seg.durationMs) / 1000, 1);
        const bool pause = seg.freqHz <= 0.0;
        const double step = pause ? 0.0 : (2.0 * kPi * seg.freqHz / sampleRateHz);
        for (int i = 0; i < n; ++i) {
            const double envPos = static_cast<double>(i) / static_cast<double>(n);
            const double env = (envPos < 0.08) ? (envPos / 0.08)
                : ((envPos > 0.92) ? ((1.0 - envPos) / 0.08) : 1.0);
            const double s = pause ? 0.0 : std::sin(phase) * env * amplitude;
            const int v = static_cast<int>(std::lround(s * 32767.0));
            out.push_back(static_cast<int16_t>(std::clamp(v, -32768, 32767)));
            phase += step;
        }
    }

    if (tailSamples > 0) {
        out.insert(out.end(), static_cast<size_t>(tailSamples), 0);
    }
    return out;
}

} // namespace owalkie::signal
