import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../platform/vibration_imitation.dart';
import '../platform/vibration_patterns.dart';
import 'shared_preferences_provider.dart';

final vibrationImitationStoreProvider = Provider<VibrationImitationStore>((ref) {
  return VibrationImitationStore(ref.watch(sharedPreferencesProvider));
});

/// Desktop vibration imitation (Windows wx [vibration_imitation_*] settings parity).
class VibrationImitationStore {
  VibrationImitationStore(this._prefs);

  static const hzKey = 'vibration_imitation_hz';
  static const volumeKey = 'vibration_imitation_volume_percent';

  final SharedPreferences _prefs;

  double freqHz() {
    final stored = _prefs.getDouble(hzKey);
    if (stored == null) {
      return VibrationImitation.defaultFreqHz;
    }
    return stored.clamp(VibrationImitation.minFreqHz, VibrationImitation.maxFreqHz);
  }

  int volumePercent() {
    final stored = _prefs.getInt(volumeKey);
    if (stored == null) {
      return VibrationImitation.defaultVolumePercent;
    }
    return stored.clamp(0, 100);
  }

  VibrationImitationSettings settings() {
    return VibrationImitationSettings(
      freqHz: freqHz(),
      volumePercent: volumePercent(),
    );
  }

  Future<void> setFreqHz(double hz) async {
    final clamped = hz.clamp(
      VibrationImitation.minFreqHz,
      VibrationImitation.maxFreqHz,
    );
    await _prefs.setDouble(hzKey, clamped);
  }

  Future<void> setVolumePercent(int percent) async {
    await _prefs.setInt(volumeKey, percent.clamp(0, 100));
  }
}
