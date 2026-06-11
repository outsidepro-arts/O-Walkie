import '../l10n/a11y_strings.dart';

/// Mirrors Kotlin [PttButtonAccessibilityBucket] for stable screen reader labels.
enum PttA11yBucket { countdown, locked, unavailable, toggle, hold }

PttA11yBucket resolvePttA11yBucket({
  required bool enabled,
  required bool active,
  required bool serverPttLocked,
  required int pttLockSec,
}) {
  final blockedByServer = serverPttLocked && !active;
  if (!enabled && blockedByServer && pttLockSec > 0) {
    return PttA11yBucket.countdown;
  }
  if (!enabled && blockedByServer) {
    return PttA11yBucket.locked;
  }
  if (!enabled) {
    return PttA11yBucket.unavailable;
  }
  if (active) {
    return PttA11yBucket.toggle;
  }
  return PttA11yBucket.hold;
}

String pttA11yLabelFor({
  required PttA11yBucket bucket,
  required int pttLockSec,
  String? frozenCountdownLabel,
}) {
  if (bucket == PttA11yBucket.countdown && frozenCountdownLabel != null) {
    return frozenCountdownLabel;
  }
  return switch (bucket) {
    PttA11yBucket.countdown => A11yStrings.pttCountdown(pttLockSec),
    PttA11yBucket.locked => A11yStrings.pttLocked,
    PttA11yBucket.unavailable => A11yStrings.pttUnavailable,
    PttA11yBucket.toggle => A11yStrings.pttToggleHint,
    PttA11yBucket.hold => A11yStrings.pttHoldHint,
  };
}
