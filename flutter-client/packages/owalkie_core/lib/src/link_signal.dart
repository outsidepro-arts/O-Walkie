/// Uplink link-signal helpers (mirrors Kotlin `(byte / 255) * 100`).
abstract final class LinkSignal {
  static int byteToPercent(int byte) {
    final clamped = byte.clamp(0, 255);
    return ((clamped / 255.0) * 100.0).round().clamp(0, 100);
  }
}
