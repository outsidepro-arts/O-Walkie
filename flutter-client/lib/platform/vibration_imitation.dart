import 'dart:math' as math;
import 'dart:typed_data';

/// Desktop "vibration" via short sine bursts (Windows [AudioEngine] parity).
abstract final class VibrationImitation {
  static const sampleRateHz = 48000;
  static const defaultFreqHz = 100.0;
  static const defaultVolumePercent = 40;
  static const gainFullScale = 0.45;
  static const minFreqHz = 30.0;
  static const maxFreqHz = 500.0;

  /// Parallel TX collision rhythm (matches mobile [0, 26, 74] ms pattern).
  static const parallelCollisionPatternMs = [26, 74];

  static Int16List synthesizePattern({
    required List<int> patternMs,
    double freqHz = defaultFreqHz,
    int volumePercent = defaultVolumePercent,
  }) {
    final gain = _gain(volumePercent);
    if (gain <= 0 || patternMs.isEmpty) {
      return Int16List(0);
    }
    final hz = freqHz.clamp(minFreqHz, maxFreqHz);
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

  static Int16List synthesizePulse(
    int durationMs, {
    double freqHz = defaultFreqHz,
    int volumePercent = defaultVolumePercent,
  }) {
    if (durationMs <= 0) {
      return Int16List(0);
    }
    return synthesizePattern(
      patternMs: [durationMs],
      freqHz: freqHz,
      volumePercent: volumePercent,
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
