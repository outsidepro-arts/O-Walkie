/// Touch-screen PTT: hold to talk; swipe up while holding to latch (messenger-style).
abstract final class PttTouchPolicy {
  static const swipeUpLockThresholdPx = 48.0;
}

enum PttPointerUpAction {
  none,
  releaseTransmit,
  releaseLatched,
}

/// True when the finger moved up far enough to latch TX.
bool pttSwipeUpLockTriggered({
  required double startY,
  required double currentY,
  double thresholdPx = PttTouchPolicy.swipeUpLockThresholdPx,
}) {
  return startY - currentY >= thresholdPx;
}

/// Resolves pointer-up after touch interaction (unit-testable).
PttPointerUpAction pttPointerUpAction({
  required bool latched,
  required bool tapToReleasePending,
}) {
  if (tapToReleasePending) {
    return PttPointerUpAction.releaseLatched;
  }
  if (!latched) {
    return PttPointerUpAction.releaseTransmit;
  }
  return PttPointerUpAction.none;
}
