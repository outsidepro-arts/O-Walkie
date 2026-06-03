#include "owalkie/udp.hpp"

namespace owalkie::pkt {

Result pack(const UdpAudioPacket& packet, std::vector<uint8_t>& out) {
    if (packet.opus.size() > 65535) {
        return Result::InvalidArg;
    }
    out.resize(9 + packet.opus.size());
    out[0] = static_cast<uint8_t>((packet.sessionId >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((packet.sessionId >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((packet.sessionId >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(packet.sessionId & 0xFF);
    out[4] = static_cast<uint8_t>((packet.sequence >> 24) & 0xFF);
    out[5] = static_cast<uint8_t>((packet.sequence >> 16) & 0xFF);
    out[6] = static_cast<uint8_t>((packet.sequence >> 8) & 0xFF);
    out[7] = static_cast<uint8_t>(packet.sequence & 0xFF);
    out[8] = packet.signalStrength;
    if (!packet.opus.empty()) {
        std::copy(packet.opus.begin(), packet.opus.end(), out.begin() + 9);
    }
    return Result::Ok;
}

Result unpack(std::span<const uint8_t> buf, UdpAudioPacket& out) {
    if (buf.size() < 9) {
        return Result::InvalidArg;
    }
    out.sessionId = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16)
        | (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
    out.sequence = (static_cast<uint32_t>(buf[4]) << 24) | (static_cast<uint32_t>(buf[5]) << 16)
        | (static_cast<uint32_t>(buf[6]) << 8) | static_cast<uint32_t>(buf[7]);
    out.signalStrength = buf[8];
    out.opus.assign(buf.begin() + 9, buf.end());
    return Result::Ok;
}

bool isKeepaliveSignal(std::span<const uint8_t> buf) {
    if (buf.size() != 9) {
        return false;
    }
    UdpAudioPacket pkt{};
    if (unpack(buf, pkt) != Result::Ok) {
        return false;
    }
    return pkt.sequence == 0 && pkt.signalStrength == kKeepaliveSignal && pkt.opus.empty();
}

bool isKeepaliveAck(std::span<const uint8_t> buf, uint32_t sessionId) {
    if (buf.size() != 9) {
        return false;
    }
    UdpAudioPacket pkt{};
    if (unpack(buf, pkt) != Result::Ok) {
        return false;
    }
    return pkt.sessionId == sessionId && pkt.sequence == 0 && pkt.signalStrength == kKeepaliveAckSignal
        && pkt.opus.empty();
}

bool isTxEofMarker(std::span<const uint8_t> buf) {
    if (buf.size() != 9) {
        return false;
    }
    UdpAudioPacket pkt{};
    if (unpack(buf, pkt) != Result::Ok) {
        return false;
    }
    return pkt.sequence > 0 && pkt.signalStrength == 0 && pkt.opus.empty();
}

} // namespace owalkie::pkt
