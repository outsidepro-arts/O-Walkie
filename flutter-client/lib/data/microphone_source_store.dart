import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'shared_preferences_provider.dart';

final microphoneSourceStoreProvider = Provider<MicrophoneSourceStore>((ref) {
  return MicrophoneSourceStore(ref.watch(sharedPreferencesProvider));
});

/// Mobile microphone profile id (Android/iOS native registries share these keys).
class MicrophoneSourceStore {
  MicrophoneSourceStore(this._prefs);

  static const key = 'microphone_source_id';
  static const defaultId = 'mic';

  final SharedPreferences _prefs;

  String selectedId() => _prefs.getString(key) ?? defaultId;

  Future<void> setSelectedId(String id) async {
    await _prefs.setString(key, id);
  }
}
