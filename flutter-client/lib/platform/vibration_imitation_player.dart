import 'dart:async';
import 'dart:io' show Platform;
import 'dart:isolate';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:owalkie_core/owalkie_core.dart';
import 'package:vibration/vibration.dart';

import 'vibration_imitation.dart';
import 'vibration_patterns.dart';

void _playPcmBlockingIsolate(_PcmPlayArgs args) {
  LocalPcmPlayer.playBlocking(args.pcm, sampleRate: args.sampleRate);
}

final class _PcmPlayArgs {
  const _PcmPlayArgs(this.pcm, this.sampleRate);

  final Int16List pcm;
  final int sampleRate;
}

/// Maps vibrator patterns to device vibrator or desktop sine imitation.
abstract final class VibrationImitationPlayer {
  static VibrationImitationSettings _settings =
      const VibrationImitationSettings.defaults();

  static void applySettings(VibrationImitationSettings settings) {
    _settings = settings;
  }

  static VibrationImitationSettings get settings => _settings;

  static bool get usesDeviceVibrator =>
      !kIsWeb && (Platform.isAndroid || Platform.isIOS);

  static bool get usesDesktopImitation =>
      !kIsWeb &&
      (Platform.isWindows || Platform.isLinux || Platform.isMacOS);

  static Future<void> playAndroidPattern(
    List<int> pattern, {
    int repeat = -1,
  }) async {
    if (usesDeviceVibrator) {
      final has = await Vibration.hasVibrator();
      if (has != true) {
        return;
      }
      await Vibration.vibrate(pattern: pattern, repeat: repeat);
      return;
    }
    if (!usesDesktopImitation) {
      return;
    }
    final pcm = VibrationImitation.synthesizeAndroidPattern(
      pattern,
      settings: _settings,
    );
    if (pcm.isEmpty) {
      return;
    }
    if (repeat == -1 || repeat >= 1) {
      LocalPcmPlayer.startLoop(pcm, sampleRate: VibrationImitation.sampleRateHz);
    }
  }

  static Future<void> stop() async {
    if (usesDeviceVibrator) {
      await Vibration.cancel();
      return;
    }
    if (usesDesktopImitation) {
      LocalPcmPlayer.stopLoop();
    }
  }

  static Future<void> playDuration(int durationMs) async {
    if (durationMs <= 0) {
      return;
    }
    if (usesDeviceVibrator) {
      final has = await Vibration.hasVibrator();
      if (has != true) {
        return;
      }
      await Vibration.vibrate(duration: durationMs);
      return;
    }
    if (!usesDesktopImitation) {
      return;
    }
    final pcm = VibrationImitation.synthesizeDuration(
      durationMs,
      settings: _settings,
    );
    if (pcm.isEmpty) {
      return;
    }
    await Isolate.run(
      () => _playPcmBlockingIsolate(
        _PcmPlayArgs(pcm, VibrationImitation.sampleRateHz),
      ),
    );
  }

  static Future<void> playPreview() async {
    if (usesDeviceVibrator) {
      await playAndroidPattern(VibrationPatterns.preview, repeat: 0);
      return;
    }
    if (!usesDesktopImitation) {
      return;
    }
    final pcm = VibrationImitation.synthesizePreview(settings: _settings);
    if (pcm.isEmpty) {
      return;
    }
    await Isolate.run(
      () => _playPcmBlockingIsolate(
        _PcmPlayArgs(pcm, VibrationImitation.sampleRateHz),
      ),
    );
  }
}
