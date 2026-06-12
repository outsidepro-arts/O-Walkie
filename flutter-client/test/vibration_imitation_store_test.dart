import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/vibration_imitation_store.dart';
import 'package:owalkie_app/platform/vibration_imitation.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('persists vibration imitation settings', () async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();
    final store = VibrationImitationStore(prefs);

    expect(store.freqHz(), VibrationImitation.defaultFreqHz);
    expect(store.volumePercent(), VibrationImitation.defaultVolumePercent);

    await store.setFreqHz(120);
    await store.setVolumePercent(55);

    expect(store.freqHz(), 120);
    expect(store.volumePercent(), 55);
    expect(store.settings().freqHz, 120);
    expect(store.settings().volumePercent, 55);
  });
}
