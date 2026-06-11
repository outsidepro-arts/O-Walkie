import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_core/owalkie_core.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('WavPcm', () {
    test('decodes spin_wt.wav from owalkie_core assets', () async {
      final data = await rootBundle.load(
        'packages/owalkie_core/assets/sounds/spin_wt.wav',
      );
      final wav = WavPcm.decode(data.buffer.asUint8List());
      expect(wav.samples, isNotEmpty);
      expect(wav.sampleRate, inInclusiveRange(4000, 192000));
    });

    test('applyGainPercent scales samples', () {
      final scaled = WavPcm.applyGainPercent(const [1000, -1000], 50);
      expect(scaled, [500, -500]);
    });

    test('resampleLinear preserves non-empty output', () {
      final out = WavPcm.resampleLinear(const [0, 1000, 0, -1000], 22050, 44100);
      expect(out.length, greaterThan(2));
    });
  });

  group('UiSoundLibrary', () {
    test('loads all UI sound assets', () async {
      await UiSoundLibrary.ensureLoaded();
      expect(UiSoundLibrary.pttPressSamples, isNotEmpty);
      expect(UiSoundLibrary.pttReleaseSamples, isNotEmpty);
    });

    test('prependPttRelease merges PCM', () {
      final merged = UiSoundLibrary.prependPttRelease(const [1, 2, 3]);
      expect(merged.length, greaterThanOrEqualTo(3));
    });
  });
}
