import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';

import '../data/vibration_imitation_store.dart';
import 'vibration_imitation_player.dart';
import 'vibration_patterns.dart';

/// Haptic feedback: device vibrator on mobile, sine imitation on desktop.
abstract final class Haptics {
  static bool _parallelActive = false;

  static bool get showsDesktopSettings =>
      !kIsWeb &&
      (Platform.isWindows || Platform.isLinux || Platform.isMacOS);

  static void applySettings(VibrationImitationSettings settings) {
    VibrationImitationPlayer.applySettings(settings);
  }

  static void applyFromStore(VibrationImitationStore store) {
    applySettings(store.settings());
  }

  static Future<void> parallelTxCollision({required bool active}) async {
    if (active) {
      if (_parallelActive) {
        return;
      }
      _parallelActive = true;
      await VibrationImitationPlayer.playAndroidPattern(
        VibrationPatterns.parallelTxCollision,
        repeat: 1,
      );
      return;
    }
    if (!_parallelActive) {
      return;
    }
    _parallelActive = false;
    await VibrationImitationPlayer.stop();
  }

  static Future<void> transmitTimeoutPulse() async {
    await VibrationImitationPlayer.playDuration(
      VibrationPatterns.transmitTimeoutDurationMs,
    );
  }

  static Future<void> scanActivityFound() async {
    await VibrationImitationPlayer.playDuration(
      VibrationPatterns.scanActivityDurationMs,
    );
  }

  static Future<void> previewDesktopImitation() async {
    await VibrationImitationPlayer.playPreview();
  }
}
