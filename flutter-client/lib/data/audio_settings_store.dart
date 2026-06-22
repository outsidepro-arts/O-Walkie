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

/// Detection mechanism for pausing relay during phone calls.
enum PhoneCallPauseMode {
  /// Native telephony listener ([TelephonyCallback]/[PhoneStateListener]).
  /// More reliable; requires [android.permission.READ_PHONE_STATE].
  telephony,

  /// Audio focus interruption via `audio_session` plugin.
  /// Simpler, no extra permissions; may miss calls when app holds no audio focus.
  audioFocus,
}

/// Pause relay transport during phone calls.
///
/// Two-level control:
/// - [isEnabled] — whether the feature is on (SwitchListTile toggle).
/// - [getMode] — which detection method to use (telephony / audioFocus).
class PhoneCallPauseStore {
  PhoneCallPauseStore(this._prefs);

  static const _enabledKey = 'phone_call_pause_enabled';
  static const _modeKey = 'phone_call_pause_mode';

  final SharedPreferences _prefs;

  bool isEnabled() => _prefs.getBool(_enabledKey) ?? false;

  Future<void> setEnabled(bool enabled) async {
    await _prefs.setBool(_enabledKey, enabled);
  }

  PhoneCallPauseMode getMode() {
    final raw = _prefs.getString(_modeKey);
    if (raw != null) {
      return PhoneCallPauseMode.values.firstWhere(
        (e) => e.name == raw,
        orElse: _defaultMode,
      );
    }
    return _defaultMode();
  }

  PhoneCallPauseMode _defaultMode() =>
      NativePlatform.isAndroid ? PhoneCallPauseMode.telephony : PhoneCallPauseMode.audioFocus;

  Future<void> setMode(PhoneCallPauseMode mode) async {
    await _prefs.setString(_modeKey, mode.name);
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
