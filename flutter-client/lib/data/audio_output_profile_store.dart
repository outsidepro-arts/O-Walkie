import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'shared_preferences_provider.dart';

final audioOutputProfileStoreProvider =
    Provider<AudioOutputProfileStore>((ref) {
  return AudioOutputProfileStore(ref.watch(sharedPreferencesProvider));
});

class AudioOutputProfileStore {
  AudioOutputProfileStore(this._prefs);

  static const _key = 'audio_output_profile_id';
  static const defaultId = 'media';

  final SharedPreferences _prefs;

  String selectedId() => _prefs.getString(_key) ?? defaultId;

  Future<void> setSelectedId(String id) async {
    await _prefs.setString(_key, id);
  }

  bool isBluetoothEnabled() {
    final id = selectedId();
    return id == 'voice_call_bt';
  }
}