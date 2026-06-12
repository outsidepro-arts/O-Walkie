import 'dart:math' as math;
import 'dart:typed_data';

import 'vibration_patterns.dart';

/// Desktop "vibration" via sine bursts (Windows [AudioEngine] parity).
abstract final class VibrationImitation {
  static const sampleRateHz = 48000;
  static const defaultFreqHz = 100.0;
  static const defaultVolumePercent = 40;
  static const gainFullScale = 0.45;
  static const minFreqHz = 30.0;
  static const maxFreqHz = 500.0;

  /// Converts Android vibrator pattern to alternating tone/silence segment lengths (ms).
  static List<int> androidPatternToToneSilenceMs(List<int> androidPattern) {
    if (androidPattern.isEmpty) {
      return const [];
    }
    final segments = <int>[];
    for (var i = 0; i < androidPattern.length; i++) {
      final ms = androidPattern[i];
      if (ms <= 0) {
        continue;
      }
      if (i.isOdd) {
        segments.add(ms);
      } else if (segments.isNotEmpty) {
        segments.add(ms);
      }
    }
    return segments;
  }

  static Int16List synthesizeAndroidPattern(
    List<int> androidPattern, {
    required VibrationImitationSettings settings,
  }) {
    return synthesizePattern(
      patternMs: androidPatternToToneSilenceMs(androidPattern),
      freqHz: settings.freqHz,
      volumePercent: settings.volumePercent,
    );
  }

  static Int16List synthesizePattern({
    required List<int> patternMs,
    double freqHz = defaultFreqHz,
    int volumePercent = defaultVolumePercent,
  }) {
    return synthesizePatternWithSettings(
      patternMs: patternMs,
      settings: VibrationImitationSettings(
        freqHz: freqHz,
        volumePercent: volumePercent,
      ),
    );
  }

  static Int16List synthesizePatternWithSettings({
    required List<int> patternMs,
    required VibrationImitationSettings settings,
  }) {
    final gain = _gain(settings.volumePercent);
    if (gain <= 0 || patternMs.isEmpty) {
      return Int16List(0);
    }
    final hz = settings.freqHz.clamp(minFreqHz, maxFreqHz);
    final out = <int>[];
    var toneOn = true;
    for (final durationMs in patternMs) {
      if (durationMs <= 0) {
        continue;
      }
      if (toneOn) {
        _appendTone(out, hz, durationMs, gain);
      } else {
        _appendSilence(out, durationMs);
      }
      toneOn = !toneOn;
    }
    return Int16List.fromList(out);
  }

  static Int16List synthesizeDuration(
    int durationMs, {
    required VibrationImitationSettings settings,
  }) {
    if (durationMs <= 0) {
      return Int16List(0);
    }
    return synthesizePatternWithSettings(
      patternMs: [durationMs],
      settings: settings,
    );
  }

  static Int16List synthesizePreview({
    required VibrationImitationSettings settings,
  }) {
    return synthesizePatternWithSettings(
      patternMs: VibrationPatterns.preview,
      settings: settings,
    );
  }

  static double _gain(int volumePercent) {
    final vol = volumePercent.clamp(0, 100);
    if (vol <= 0) {
      return 0;
    }
    return (vol / 100.0) * gainFullScale;
  }

  static void _appendSilence(List<int> out, int durationMs) {
    final samples = (sampleRateHz * durationMs) ~/ 1000;
    if (samples <= 0) {
      return;
    }
    out.addAll(List<int>.filled(samples, 0));
  }

  static void _appendTone(
    List<int> out,
    double freqHz,
    int durationMs,
    double gain,
  ) {
    final samples = (sampleRateHz * durationMs) ~/ 1000;
    if (samples <= 0 || freqHz <= 0) {
      return;
    }
    const pi = math.pi;
    final w = (2 * pi * freqHz) / sampleRateHz;
    for (var i = 0; i < samples; i++) {
      final env = math.sin(pi * i / math.max(samples - 1, 1));
      final s = math.sin(w * i) * env * gain;
      final v = (s * 32767).round().clamp(-32768, 32767);
      out.add(v);
    }
  }
}
