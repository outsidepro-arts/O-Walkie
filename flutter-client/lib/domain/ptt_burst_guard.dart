import 'dart:async';

/// Blocks PTT after rapid releases (Kotlin [WalkieService] burst guard).
class PttBurstGuard {
  static const releaseBurstTimerMs = 1000;
  static const releaseBurstBlockThreshold = 3;

  PttBurstGuard({this.onBlockedChanged});

  void Function(bool blocked)? onBlockedChanged;

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
      _setBlocked(true);
    }
    _scheduleDecay();
  }

  void reset() {
    _decayTimer?.cancel();
    _decayTimer = null;
    _releaseCount = 0;
    _setBlocked(false);
  }

  void _setBlocked(bool value) {
    if (_pressBlocked == value) {
      return;
    }
    _pressBlocked = value;
    onBlockedChanged?.call(value);
  }

  void _scheduleDecay() {
    _decayTimer?.cancel();
    _decayTimer = Timer(
      const Duration(milliseconds: releaseBurstTimerMs),
      () {
        _releaseCount = 0;
        _setBlocked(false);
      },
    );
  }
}
