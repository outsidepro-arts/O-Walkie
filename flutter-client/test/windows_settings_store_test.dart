import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/windows_settings_store.dart';
import 'package:owalkie_app/domain/windows_ptt_binding.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('WindowsSettingsStore', () {
    setUp(() async {
      SharedPreferences.setMockInitialValues({});
    });

    test('minimize to tray defaults to false', () async {
      final prefs = await SharedPreferences.getInstance();
      final store = WindowsSettingsStore(prefs);
      expect(store.minimizeToTrayOnClose(), isFalse);
    });

    test('persists minimize to tray', () async {
      final prefs = await SharedPreferences.getInstance();
      final store = WindowsSettingsStore(prefs);
      await store.setMinimizeToTrayOnClose(true);
      expect(store.minimizeToTrayOnClose(), isTrue);
    });

    test('loadBinding returns null when unset', () async {
      final prefs = await SharedPreferences.getInstance();
      final store = WindowsSettingsStore(prefs);
      expect(store.loadBinding(), isNull);
    });

    test('persists global PTT binding', () async {
      final prefs = await SharedPreferences.getInstance();
      final store = WindowsSettingsStore(prefs);
      const binding = WindowsPttBinding(
        vkey: 0x46,
        mods: 2,
        displayName: 'Ctrl+F',
      );
      await store.saveBinding(binding);
      expect(store.loadBinding(), binding);
    });
  });
}
