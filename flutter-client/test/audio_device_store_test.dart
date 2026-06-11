import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/audio_device_store.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('persists input and output device selection', () async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();
    final store = AudioDeviceStore(prefs);

    expect(store.inputDeviceIndex(), -1);
    expect(store.outputDeviceIndex(), -1);
    expect(store.inputPlatformId(), isNull);

    await store.setInputDevice(index: 2, platformId: 42);
    await store.setOutputDevice(index: 1, platformId: 7);

    expect(store.inputDeviceIndex(), 2);
    expect(store.outputDeviceIndex(), 1);
    expect(store.inputPlatformId(), 42);
    expect(store.outputPlatformId(), 7);
  });
}
