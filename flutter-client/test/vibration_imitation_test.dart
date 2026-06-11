import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/platform/vibration_imitation.dart';

void main() {
  test('synthesizePulse produces expected duration at 48 kHz', () {
    const durationMs = 50;
    final pcm = VibrationImitation.synthesizePulse(durationMs);
    expect(pcm.length, (VibrationImitation.sampleRateHz * durationMs) ~/ 1000);
  });

  test('synthesizePattern alternates tone and silence segments', () {
    final pcm = VibrationImitation.synthesizePattern(
      patternMs: [26, 74],
      volumePercent: 40,
    );
    expect(pcm.length, (VibrationImitation.sampleRateHz * 100) ~/ 1000);
    expect(pcm.any((s) => s != 0), isTrue);
    expect(pcm.any((s) => s == 0), isTrue);
  });

  test('zero volume yields empty pcm', () {
    final pcm = VibrationImitation.synthesizePulse(50, volumePercent: 0);
    expect(pcm, isEmpty);
  });
}
