import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:hotkey_manager/hotkey_manager.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'shared_preferences_provider.dart';

final windowsSettingsStoreProvider = Provider<WindowsSettingsStore>((ref) {
  return WindowsSettingsStore(ref.watch(sharedPreferencesProvider));
});

/// Windows desktop preferences (global PTT hotkey, minimize-to-tray).
class WindowsSettingsStore {
  WindowsSettingsStore(this._prefs);

  static const _hotKeyKey = 'windows_global_ptt_hotkey';
  static const _minimizeToTrayKey = 'windows_minimize_to_tray_on_close';

  final SharedPreferences _prefs;

  HotKey? loadHotKey() {
    final raw = _prefs.getString(_hotKeyKey);
    if (raw == null || raw.isEmpty) {
      return null;
    }
    try {
      final json = jsonDecode(raw);
      if (json is! Map<String, dynamic>) {
        return null;
      }
      return HotKey.fromJson(json);
    } catch (_) {
      return null;
    }
  }

  Future<void> saveHotKey(HotKey? hotKey) async {
    if (hotKey == null) {
      await _prefs.remove(_hotKeyKey);
      return;
    }
    await _prefs.setString(_hotKeyKey, jsonEncode(hotKey.toJson()));
  }

  bool minimizeToTrayOnClose() => _prefs.getBool(_minimizeToTrayKey) ?? false;

  Future<void> setMinimizeToTrayOnClose(bool enabled) async {
    await _prefs.setBool(_minimizeToTrayKey, enabled);
  }
}
