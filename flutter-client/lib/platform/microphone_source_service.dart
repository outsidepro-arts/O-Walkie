import 'dart:io';

import 'package:owalkie_core/owalkie_core.dart';

import '../data/microphone_source_store.dart';
import '../domain/microphone_source_option.dart';
import 'native_platform.dart';

/// Mobile microphone profile selection (Android AAudio preset / iOS AVAudioSession mode).
abstract final class MicrophoneSourceService {
  static bool get isSupported => Platform.isAndroid || Platform.isIOS;

  static Future<List<MicrophoneSourceOption>> listOptions() async {
    if (!isSupported) {
      return const [];
    }
    return NativePlatform.listMicrophoneSources();
  }

  static Future<void> applyFromStore(
    MicrophoneSourceStore store, {
    bool bluetoothHeadset = false,
  }) async {
    if (!isSupported) {
      return;
    }
    final options = await listOptions();
    if (options.isEmpty) {
      return;
    }
    final selected = resolveStored(store, options);
    await applyProfile(
      selected,
      bluetoothHeadset: bluetoothHeadset,
    );
  }

  static Future<void> persistAndApply({
    required MicrophoneSourceStore store,
    required MicrophoneSourceOption option,
    bool bluetoothHeadset = false,
  }) async {
    await store.setSelectedId(option.id);
    await applyProfile(option, bluetoothHeadset: bluetoothHeadset);
  }

  static Future<void> applyProfile(
    MicrophoneSourceOption option, {
    bool bluetoothHeadset = false,
  }) async {
    if (Platform.isIOS) {
      await NativePlatform.applyMicrophoneProfile(
        option.id,
        bluetoothHeadset: bluetoothHeadset,
      );
      return;
    }
    if (Platform.isAndroid) {
      AudioDeviceBindings.open().setCaptureAaudioInputPreset(option.inputPreset);
    }
  }

  static MicrophoneSourceOption resolveStored(
    MicrophoneSourceStore store,
    List<MicrophoneSourceOption> options,
  ) {
    if (options.isEmpty) {
      throw StateError('No microphone sources');
    }
    return options.firstWhere(
      (o) => o.id == store.selectedId(),
      orElse: () => options.first,
    );
  }
}
