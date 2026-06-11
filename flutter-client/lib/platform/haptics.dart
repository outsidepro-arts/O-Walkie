import 'dart:async';
import 'dart:io' show Platform;
import 'dart:isolate';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:owalkie_core/owalkie_core.dart';
import 'package:vibration/vibration.dart';

import 'vibration_imitation.dart';

void _playVibrationImitationPcmIsolate(_VibrationPlayArgs args) {
  LocalPcmPlayer.playBlocking(args.pcm, sampleRate: args.sampleRate);
}

final class _VibrationPlayArgs {
  const _VibrationPlayArgs(this.pcm, this.sampleRate);

  final Int16List pcm;
  final int sampleRate;
}

/// Haptic feedback: device vibrator on mobile, sine-burst imitation on desktop.
abstract final class Haptics {
  static bool _desktopParallelLoopActive = false;
  static Future<void>? _desktopParallelLoop;

  static bool get _useDeviceVibrator =>
      !kIsWeb && (Platform.isAndroid || Platform.isIOS);

  static bool get _useDesktopImitation =>
      !kIsWeb &&
      (Platform.isWindows || Platform.isLinux || Platform.isMacOS);

  static Future<void> parallelTxCollision({required bool active}) async {
    if (_useDeviceVibrator) {
      await _parallelTxCollisionMobile(active: active);
      return;
    }
    if (_useDesktopImitation) {
      await _parallelTxCollisionDesktop(active: active);
    }
  }

  static Future<void> transmitTimeoutPulse() async {
    if (_useDeviceVibrator) {
      await _pulseMobile(durationMs: 50);
      return;
    }
    if (_useDesktopImitation) {
      await _playDesktopPulse(50);
    }
  }

  static Future<void> scanActivityFound({int durationMs = 200}) async {
    if (_useDeviceVibrator) {
      await _pulseMobile(durationMs: durationMs);
      return;
    }
    if (_useDesktopImitation) {
      await _playDesktopPulse(durationMs);
    }
  }

  static Future<void> _parallelTxCollisionMobile({required bool active}) async {
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

  static Future<void> _parallelTxCollisionDesktop({required bool active}) async {
    if (active) {
      if (_desktopParallelLoopActive) {
        return;
      }
      _desktopParallelLoopActive = true;
      final pcm = VibrationImitation.synthesizePattern(
        patternMs: VibrationImitation.parallelCollisionPatternMs,
      );
      if (pcm.isEmpty) {
        _desktopParallelLoopActive = false;
        return;
      }
      _desktopParallelLoop = _runDesktopParallelLoop(pcm);
      return;
    }
    _desktopParallelLoopActive = false;
    final loop = _desktopParallelLoop;
    if (loop != null) {
      await loop;
    }
    _desktopParallelLoop = null;
  }

  static Future<void> _runDesktopParallelLoop(Int16List pcm) async {
    final args = _VibrationPlayArgs(pcm, VibrationImitation.sampleRateHz);
    while (_desktopParallelLoopActive) {
      await Isolate.run(() => _playVibrationImitationPcmIsolate(args));
    }
  }

  static Future<void> _pulseMobile({required int durationMs}) async {
    final has = await Vibration.hasVibrator();
    if (has != true) {
      return;
    }
    await Vibration.vibrate(duration: durationMs);
  }

  static Future<void> _playDesktopPulse(int durationMs) async {
    final pcm = VibrationImitation.synthesizePulse(durationMs);
    if (pcm.isEmpty) {
      return;
    }
    await Isolate.run(
      () => _playVibrationImitationPcmIsolate(
        _VibrationPlayArgs(pcm, VibrationImitation.sampleRateHz),
      ),
    );
  }
}
