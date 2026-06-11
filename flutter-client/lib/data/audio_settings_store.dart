import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../platform/native_platform.dart';
import 'shared_preferences_provider.dart';

final phoneCallPauseStoreProvider = Provider<PhoneCallPauseStore>((ref) {
  return PhoneCallPauseStore(ref.watch(sharedPreferencesProvider));
});

final bluetoothHeadsetStoreProvider = Provider<BluetoothHeadsetStore>((ref) {
  return BluetoothHeadsetStore(ref.watch(sharedPreferencesProvider));
});

final mediaButtonPttStoreProvider = Provider<MediaButtonPttStore>((ref) {
  return MediaButtonPttStore(ref.watch(sharedPreferencesProvider));
});

final externalControlStoreProvider = Provider<ExternalControlStore>((ref) {
  return ExternalControlStore();
});

/// Pause relay transport during phone calls (Kotlin [PhoneCallRelayPauseStore]).
class PhoneCallPauseStore {
  PhoneCallPauseStore(this._prefs);

  static const _key = 'pause_during_cellular_call';

  final SharedPreferences _prefs;

  bool isEnabled() => _prefs.getBool(_key) ?? true;

  Future<void> setEnabled(bool enabled) async {
    await _prefs.setBool(_key, enabled);
  }
}

/// Prefer Bluetooth headset mic/route when available.
class BluetoothHeadsetStore {
  BluetoothHeadsetStore(this._prefs);

  static const _key = 'bluetooth_headset_route_enabled';

  final SharedPreferences _prefs;

  bool isEnabled() => _prefs.getBool(_key) ?? false;

  Future<void> setEnabled(bool enabled) async {
    await _prefs.setBool(_key, enabled);
  }
}

/// Headset / media play-pause toggles TX latch (Kotlin [PttHardwareKeyStore] media flag).
class MediaButtonPttStore {
  MediaButtonPttStore(this._prefs);

  static const _key = 'media_button_ptt';

  final SharedPreferences _prefs;

  bool isEnabled() => _prefs.getBool(_key) ?? true;

  Future<void> setEnabled(bool enabled) async {
    await _prefs.setBool(_key, enabled);
  }
}

/// Tasker / automation broadcast API gate (Kotlin [ExternalControlStore]).
class ExternalControlStore {
  Future<bool> isEnabled() => NativePlatform.getExternalControlEnabled();

  Future<void> setEnabled(bool enabled) =>
      NativePlatform.setExternalControlEnabled(enabled);
}
