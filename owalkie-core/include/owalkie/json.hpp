#pragma once

#include <string>
#include <string_view>

#include "owalkie/types.hpp"

namespace owalkie::json {

Result parseWelcome(
    std::string_view text,
    WelcomeConfig& out,
    int requiredProtocolVersion = kProtocolVersion);

Result parseServerMessage(std::string_view text, Event& out);

std::string buildJoin(std::string_view channel);
std::string buildHasActivity(std::string_view channel);
std::string buildUdpHello(int localUdpPort);
std::string buildRepeaterMode(bool enabled);
std::string buildHeartbeat();

/** Parses @c {"type":"has_activity","active":...} response. */
Result parseHasActivityResponse(std::string_view text, bool& outActive);

} // namespace owalkie::json
