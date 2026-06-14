import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/warm_mic_recorder_store.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  test('warm mic recorder defaults to disabled', () async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();
    final store = WarmMicRecorderStore(prefs);

    expect(store.isEnabled(), isFalse);

    await store.setEnabled(true);
    expect(store.isEnabled(), isTrue);
  });
}
