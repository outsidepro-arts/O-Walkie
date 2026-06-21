import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'shared_preferences_provider.dart';

final rxVolumeStoreProvider = Provider<RxVolumeStore>((ref) {
  return RxVolumeStore(ref.watch(sharedPreferencesProvider));
});

class RxVolumeStore {
  RxVolumeStore(this._prefs);

  static const _key = 'rx_volume_percent';
  final SharedPreferences _prefs;

  int getVolume() => _prefs.getInt(_key) ?? 100;

  Future<void> setVolume(int percent) async {
    await _prefs.setInt(_key, percent.clamp(0, 200));
  }
}
