import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:vibration/vibration.dart';

/// Haptic feedback (mobile only; no-op elsewhere).
abstract final class Haptics {
  static Future<void> parallelTxCollision({required bool active}) async {
    if (kIsWeb || !Platform.isAndroid && !Platform.isIOS) {
      return;
    }
    final has = await Vibration.hasVibrator();
    if (has != true) {
      return;
    }
    if (active) {
      await Vibration.vibrate(pattern: [0, 26, 74], repeat: 1);
    } else {
      await Vibration.cancel();
    }
  }

  static Future<void> transmitTimeoutPulse() async {
    if (kIsWeb || !Platform.isAndroid && !Platform.isIOS) {
      return;
    }
    final has = await Vibration.hasVibrator();
    if (has != true) {
      return;
    }
    await Vibration.vibrate(duration: 50);
  }
}
