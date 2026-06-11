import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_core/src/signal_pattern_pcm.dart';
import 'package:owalkie_core/src/signal_point.dart';

void main() {
  test('empty pattern produces no samples', () {
    expect(SignalPatternPcm.synthesize([]), isEmpty);
  });

  test('pause segment produces silence', () {
    final pcm = SignalPatternPcm.synthesize(
      const [SignalPoint(freqHz: 0, durationMs: 20)],
    );
    expect(pcm, isNotEmpty);
    expect(pcm.every((s) => s == 0), isTrue);
  });

  test('tone segment produces non-zero samples', () {
    final pcm = SignalPatternPcm.synthesize(
      const [SignalPoint(freqHz: 1000, durationMs: 50)],
    );
    expect(pcm.length, greaterThan(100));
    expect(pcm.any((s) => s != 0), isTrue);
  });
}
