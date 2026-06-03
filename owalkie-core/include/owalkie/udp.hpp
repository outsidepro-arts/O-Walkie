#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "owalkie/types.hpp"

namespace owalkie::pkt {

constexpr uint8_t kKeepaliveSignal = 255;
constexpr uint8_t kKeepaliveAckSignal = 254;
constexpr uint8_t kDefaultTxSignalStrength = 255;

Result pack(const UdpAudioPacket& packet, std::vector<uint8_t>& out);
Result unpack(std::span<const uint8_t> buf, UdpAudioPacket& out);

bool isKeepaliveSignal(std::span<const uint8_t> buf);
bool isKeepaliveAck(std::span<const uint8_t> buf, uint32_t sessionId);
bool isTxEofMarker(std::span<const uint8_t> buf);

} // namespace owalkie::pkt
