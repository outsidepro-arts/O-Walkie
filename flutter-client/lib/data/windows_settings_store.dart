import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../domain/windows_ptt_binding.dart';
import 'shared_preferences_provider.dart';

final windowsSettingsStoreProvider = Provider<WindowsSettingsStore>((ref) {
  return WindowsSettingsStore(ref.watch(sharedPreferencesProvider));
});

/// Windows desktop preferences (global PTT binding, minimize-to-tray).
class WindowsSettingsStore {
  WindowsSettingsStore(this._prefs);

  static const _bindingKey = 'windows_global_ptt_binding';
  static const _legacyHotKeyKey = 'windows_global_ptt_hotkey';
  static const _minimizeToTrayKey = 'windows_minimize_to_tray_on_close';

  final SharedPreferences _prefs;

  WindowsPttBinding? loadBinding() {
    final raw = _prefs.getString(_bindingKey);
    if (raw == null || raw.isEmpty) {
      return null;
    }
    try {
      final json = jsonDecode(raw);
      if (json is! Map<String, dynamic>) {
        return null;
      }
      final binding = WindowsPttBinding.fromJson(json);
      return binding.assigned ? binding : null;
    } catch (_) {
      return null;
    }
  }

  Future<void> saveBinding(WindowsPttBinding? binding) async {
    if (binding == null || !binding.assigned) {
      await _prefs.remove(_bindingKey);
      return;
    }
    await _prefs.setString(_bindingKey, jsonEncode(binding.toJson()));
    await _prefs.remove(_legacyHotKeyKey);
  }

  bool minimizeToTrayOnClose() => _prefs.getBool(_minimizeToTrayKey) ?? false;

  Future<void> setMinimizeToTrayOnClose(bool enabled) async {
    await _prefs.setBool(_minimizeToTrayKey, enabled);
  }
}
