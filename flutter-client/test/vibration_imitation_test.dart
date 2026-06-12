import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/platform/vibration_imitation.dart';
import 'package:owalkie_app/platform/vibration_patterns.dart';

void main() {
  test('androidPatternToToneSilenceMs maps parallel collision pattern', () {
    expect(
      VibrationImitation.androidPatternToToneSilenceMs(
        VibrationPatterns.parallelTxCollision,
      ),
      [26, 74],
    );
  });

  test('synthesizeDuration matches wall-clock sample count', () {
    const durationMs = 50;
    final pcm = VibrationImitation.synthesizeDuration(
      durationMs,
      settings: const VibrationImitationSettings.defaults(),
    );
    expect(pcm.length, (VibrationImitation.sampleRateHz * durationMs) ~/ 1000);
  });

  test('synthesizeAndroidPattern covers one full vibrator cycle', () {
    final pcm = VibrationImitation.synthesizeAndroidPattern(
      VibrationPatterns.parallelTxCollision,
      settings: const VibrationImitationSettings.defaults(),
    );
    expect(pcm.length, (VibrationImitation.sampleRateHz * 100) ~/ 1000);
    expect(pcm.any((s) => s != 0), isTrue);
    expect(pcm.any((s) => s == 0), isTrue);
  });

  test('zero volume yields empty pcm', () {
    final pcm = VibrationImitation.synthesizeDuration(
      50,
      settings: const VibrationImitationSettings(volumePercent: 0, freqHz: 100),
    );
    expect(pcm, isEmpty);
  });
}
