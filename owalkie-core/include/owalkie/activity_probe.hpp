#pragma once

#include "owalkie/types.hpp"

namespace owalkie::activity_probe {

/**
 * Short-lived WebSocket probe: welcome → has_activity → response.
 * Does not create a managed session. @p outActive is set on @c Result::Ok.
 */
Result checkChannelActivity(const ConnectParams& params, int timeoutMs, bool& outActive);

} // namespace owalkie::activity_probe
