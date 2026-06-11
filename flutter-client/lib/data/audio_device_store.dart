import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'shared_preferences_provider.dart';

final audioDeviceStoreProvider = Provider<AudioDeviceStore>((ref) {
  return AudioDeviceStore(ref.watch(sharedPreferencesProvider));
});

/// Persisted audio device selection (miniaudio index on desktop, platform id on Android).
class AudioDeviceStore {
  AudioDeviceStore(this._prefs);

  static const inputKey = 'audio_input_device';
  static const outputKey = 'audio_output_device';
  static const inputPlatformIdKey = 'audio_input_platform_id';
  static const outputPlatformIdKey = 'audio_output_platform_id';

  final SharedPreferences _prefs;

  int inputDeviceIndex() => _prefs.getInt(inputKey) ?? -1;

  int outputDeviceIndex() => _prefs.getInt(outputKey) ?? -1;

  int? inputPlatformId() {
    if (!_prefs.containsKey(inputPlatformIdKey)) {
      return null;
    }
    return _prefs.getInt(inputPlatformIdKey);
  }

  int? outputPlatformId() {
    if (!_prefs.containsKey(outputPlatformIdKey)) {
      return null;
    }
    return _prefs.getInt(outputPlatformIdKey);
  }

  Future<void> setInputDevice({
    required int index,
    int? platformId,
  }) async {
    await _prefs.setInt(inputKey, index);
    if (platformId == null) {
      await _prefs.remove(inputPlatformIdKey);
    } else {
      await _prefs.setInt(inputPlatformIdKey, platformId);
    }
  }

  Future<void> setOutputDevice({
    required int index,
    int? platformId,
  }) async {
    await _prefs.setInt(outputKey, index);
    if (platformId == null) {
      await _prefs.remove(outputPlatformIdKey);
    } else {
      await _prefs.setInt(outputPlatformIdKey, platformId);
    }
  }
}
