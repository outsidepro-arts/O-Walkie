import 'dart:async';

/// Blocks PTT after rapid releases (Kotlin [WalkieService] burst guard).
class PttBurstGuard {
  static const releaseBurstTimerMs = 1000;
  static const releaseBurstBlockThreshold = 3;

  int _releaseCount = 0;
  bool _pressBlocked = false;
  Timer? _decayTimer;

  bool get pressBlocked => _pressBlocked;

  /// @return true when press is allowed.
  bool onPressAttempt() {
    if (_pressBlocked) {
      _scheduleDecay();
      return false;
    }
    return true;
  }

  void onRelease() {
    _releaseCount++;
    if (_releaseCount >= releaseBurstBlockThreshold) {
      _pressBlocked = true;
    }
    _scheduleDecay();
  }

  void reset() {
    _decayTimer?.cancel();
    _decayTimer = null;
    _releaseCount = 0;
    _pressBlocked = false;
  }

  void _scheduleDecay() {
    _decayTimer?.cancel();
    _decayTimer = Timer(
      const Duration(milliseconds: releaseBurstTimerMs),
      () {
        _releaseCount = 0;
        _pressBlocked = false;
      },
    );
  }
}
