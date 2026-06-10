/// Mirrors [owalkie_event_type] from owalkie_core.h.
abstract final class OwalkieEventType {
  static const connected = 0;
  static const disconnected = 1;
  static const protocolError = 2;
  static const connectionFailed = 3;
  static const rxBroadcastStart = 4;
  static const rxBroadcastEnd = 5;
  static const pttLocked = 6;
  static const pttUnlocked = 7;
  static const txCountdownStart = 8;
  static const txStop = 9;
  static const connectionLost = 10;
}
