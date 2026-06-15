import 'dart:io';

import 'package:owalkie_core/owalkie_core.dart';

import '../data/audio_output_profile_store.dart';
import '../domain/audio_output_profile.dart';
import 'native_platform.dart';

abstract final class AudioOutputProfileService {
  static bool get isSupported => Platform.isAndroid || Platform.isIOS;

  static Future<void> applyFromStore(AudioOutputProfileStore store) async {
    if (!isSupported) {
      return;
    }
    final profile = AudioOutputProfile.byId(store.selectedId());
    await applyProfile(profile);
  }

  static Future<void> persistAndApply({
    required AudioOutputProfileStore store,
    required AudioOutputProfile profile,
  }) async {
    await store.setSelectedId(profile.id);
    await applyProfile(profile);
  }

  static Future<void> applyProfile(AudioOutputProfile profile) async {
    if (Platform.isAndroid) {
      final bindings = AudioDeviceBindings.open();
      bindings.setPlaybackAaudioUsage(profile.aaudioUsage);
      bindings.setPlaybackAaudioContentType(profile.aaudioContentType);
      bindings.setCaptureAaudioUsage(profile.aaudioUsage);
      bindings.setAndroidBtVoiceRoute(profile.enableBtSco);
    }
    if (Platform.isIOS) {
      await NativePlatform.applyAudioOutputProfile(
        profileId: profile.id,
        iosMode: profile.iosMode,
        iosDefaultToSpeaker: profile.iosDefaultToSpeaker,
        iosAllowBluetoothA2dp: profile.iosAllowBluetoothA2dp,
        iosDuckOthers: profile.iosDuckOthers,
      );
    }
    if (Platform.isAndroid) {
      await NativePlatform.applyAudioOutputProfile(
        profileId: profile.id,
        audioManagerMode: profile.audioManagerMode,
        enableBtSco: profile.enableBtSco,
      );
    }
  }

  static AudioOutputProfile resolveStored(AudioOutputProfileStore store) {
    return AudioOutputProfile.byId(store.selectedId());
  }
}