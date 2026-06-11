/// Touch-screen PTT: short tap toggles TX latch; hold behaves as classic PTT.
abstract final class PttTouchPolicy {
  static const holdThresholdMs = 280;
}

enum PttPointerUpAction {
  none,
  toggleOn,
  toggleOff,
  releaseHold,
}

/// Resolves pointer-up after touch interaction (unit-testable).
PttPointerUpAction pttPointerUpAction({
  required bool holdMode,
  required bool txActive,
}) {
  if (holdMode) {
    return PttPointerUpAction.releaseHold;
  }
  return txActive ? PttPointerUpAction.toggleOff : PttPointerUpAction.toggleOn;
}
