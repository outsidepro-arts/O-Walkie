import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/microphone_source_store.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('persists microphone source id', () async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();
    final store = MicrophoneSourceStore(prefs);

    expect(store.selectedId(), MicrophoneSourceStore.defaultId);

    await store.setSelectedId(MicrophoneSourceStore.defaultId);
    expect(store.selectedId(), 'mic');

    await store.setSelectedId('unprocessed');
    expect(store.selectedId(), 'unprocessed');
  });
}
