import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'shared_preferences_provider.dart';

final warmMicRecorderStoreProvider = Provider<WarmMicRecorderStore>((ref) {
  return WarmMicRecorderStore(ref.watch(sharedPreferencesProvider));
});

/// Kotlin [WarmMicRecorderStore] — optional warm capture while foreground + connected.
class WarmMicRecorderStore {
  WarmMicRecorderStore(this._prefs);

  static const key = 'warm_mic_recorder_enabled';

  final SharedPreferences _prefs;

  bool isEnabled() => _prefs.getBool(key) ?? false;

  Future<void> setEnabled(bool enabled) async {
    await _prefs.setBool(key, enabled);
  }
}
