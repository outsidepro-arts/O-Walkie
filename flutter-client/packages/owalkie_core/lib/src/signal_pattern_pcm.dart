import 'dart:math' as math;

import 'signal_point.dart';

/// Kotlin [SignalPreviewPlayer] PCM synthesis (44.1 kHz, no Roger tail).
abstract final class SignalPatternPcm {
  static const sampleRate = 44100;
  static const previewGain = 0.26;

  static List<int> synthesize(
    List<SignalPoint> points, {
    double gain = previewGain,
  }) {
    if (points.isEmpty) {
      return const [];
    }
    final total = points.fold<int>(
      0,
      (sum, p) => sum + (sampleRate * p.durationMs) ~/ 1000,
    );
    final out = List<int>.filled(total.clamp(1, total), 0);
    var idx = 0;
    var phase = 0.0;
    for (final point in points) {
      final n = ((sampleRate * point.durationMs) / 1000).clamp(1, 1 << 20);
      final isPause = point.freqHz <= 0;
      final step = isPause
          ? 0.0
          : 2 * math.pi * point.freqHz / sampleRate;
      for (var i = 0; i < n; i++) {
        if (idx >= out.length) {
          break;
        }
        final envPos = i / n;
        final env = envPos < 0.08
            ? envPos / 0.08
            : envPos > 0.92
                ? (1 - envPos) / 0.08
                : 1.0;
        final sample = isPause ? 0.0 : math.sin(phase) * env * gain;
        out[idx] = (sample * 32767).round().clamp(-32768, 32767);
        idx++;
        phase += step;
      }
    }
    return idx == out.length ? out : out.sublist(0, idx);
  }
}
