import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:owalkie_core/owalkie_core.dart';

import '../data/audio_device_store.dart';
import '../data/microphone_source_store.dart';
import 'microphone_source_service.dart';

export 'package:owalkie_core/owalkie_core.dart' show NativeAudioDevice;

/// App facade for audio device list/selection (Windows client parity).
abstract final class AudioDeviceService {
  /// miniaudio preferred index: follow OS default device (Windows C++ client parity).
  static const systemDefaultIndex = -1;

  static bool get isSupported => AudioDeviceBindings.isPlatformSupported;

  /// Desktop miniaudio path: explicit "System default" dropdown entry.
  static bool get supportsSystemDefaultOption =>
      Platform.isWindows || Platform.isIOS;

  static bool get showsPhysicalAudioDevices =>
      isSupported && !Platform.isAndroid;

  static Future<List<NativeAudioDevice>> listInputDevices() async {
    if (!showsPhysicalAudioDevices) {
      return const [];
    }
    try {
      return AudioDeviceBindings.open().listCaptureDevices();
    } catch (e, st) {
      assert(() {
        debugPrint('AudioDeviceService.listInputDevices failed: $e\n$st');
        return true;
      }());
      return const [];
    }
  }

  static Future<List<NativeAudioDevice>> listOutputDevices() async {
    if (!showsPhysicalAudioDevices) {
      return const [];
    }
    try {
      return AudioDeviceBindings.open().listPlaybackDevices();
    } catch (e, st) {
      assert(() {
        debugPrint('AudioDeviceService.listOutputDevices failed: $e\n$st');
        return true;
      }());
      return const [];
    }
  }

  static int defaultDeviceIndex(List<NativeAudioDevice> devices) {
    if (devices.isEmpty) {
      return -1;
    }
    for (final d in devices) {
      if (d.isDefault) {
        return d.index;
      }
    }
    return devices.first.index;
  }

  static NativeAudioDevice? deviceByIndex(
    int index,
    List<NativeAudioDevice> devices,
  ) {
    for (final d in devices) {
      if (d.index == index) {
        return d;
      }
    }
    return null;
  }

  static int resolveStoredIndex(int stored, List<NativeAudioDevice> devices) {
    if (devices.isEmpty) {
      return systemDefaultIndex;
    }
    if (stored == systemDefaultIndex && supportsSystemDefaultOption) {
      return systemDefaultIndex;
    }
    if (stored >= 0 && devices.any((d) => d.index == stored)) {
      return stored;
    }
    return defaultDeviceIndex(devices);
  }

  /// Dropdown value: keep [-1] when user chose system default.
  static int dropdownValue(int stored, List<NativeAudioDevice> devices) {
    if (supportsSystemDefaultOption && stored == systemDefaultIndex) {
      return systemDefaultIndex;
    }
    return resolveStoredIndex(stored, devices);
  }

  static int applyIndex(int stored, List<NativeAudioDevice> devices) {
    if (supportsSystemDefaultOption && stored == systemDefaultIndex) {
      return systemDefaultIndex;
    }
    return resolveStoredIndex(stored, devices);
  }

  static NativeAudioDevice resolveStoredDevice(
    AudioDeviceStore store,
    List<NativeAudioDevice> devices, {
    required bool input,
  }) {
    if (devices.isEmpty) {
      throw StateError('No audio devices');
    }
    if (Platform.isAndroid) {
      final platformId = input ? store.inputPlatformId() : store.outputPlatformId();
      if (platformId != null) {
        for (final d in devices) {
          if (d.platformId == platformId) {
            return d;
          }
        }
      }
    }
    final storedIndex = input ? store.inputDeviceIndex() : store.outputDeviceIndex();
    final index = resolveStoredIndex(storedIndex, devices);
    return deviceByIndex(index, devices) ?? devices.first;
  }

  static void applyDevice(
    AudioDeviceBindings bindings, {
    required NativeAudioDevice input,
    required NativeAudioDevice output,
  }) {
    bindings.applySelection(
      inputIndex: input.index,
      outputIndex: output.index,
      inputPlatformId: input.platformId,
      outputPlatformId: output.platformId,
    );
  }

  static Future<void> applyFromStore(
    AudioDeviceStore store, {
    MicrophoneSourceStore? microphoneStore,
    bool bluetoothHeadset = false,
  }) async {
    if (!isSupported) {
      return;
    }
    try {
      if (Platform.isAndroid || Platform.isIOS) {
        if (microphoneStore != null) {
          await MicrophoneSourceService.applyFromStore(
            microphoneStore,
            bluetoothHeadset: bluetoothHeadset,
          );
        }
        return;
      }
      final bindings = AudioDeviceBindings.open();
      final inputs = await listInputDevices();
      final outputs = await listOutputDevices();
      if (inputs.isEmpty || outputs.isEmpty) {
        return;
      }
      bindings.applySelection(
        inputIndex: applyIndex(store.inputDeviceIndex(), inputs),
        outputIndex: applyIndex(store.outputDeviceIndex(), outputs),
      );
    } catch (_) {}
  }

  static Future<void> persistAndApplyInput({
    required AudioDeviceStore store,
    required int index,
    required List<NativeAudioDevice> inputDevices,
    required List<NativeAudioDevice> outputDevices,
    required int outputIndex,
  }) async {
    await store.setInputDevice(index: index);
    AudioDeviceBindings.open().applySelection(
      inputIndex: applyIndex(index, inputDevices),
      outputIndex: applyIndex(outputIndex, outputDevices),
    );
  }

  static Future<void> persistAndApplyOutput({
    required AudioDeviceStore store,
    required int index,
    required List<NativeAudioDevice> inputDevices,
    required List<NativeAudioDevice> outputDevices,
    required int inputIndex,
  }) async {
    await store.setOutputDevice(index: index);
    AudioDeviceBindings.open().applySelection(
      inputIndex: applyIndex(inputIndex, inputDevices),
      outputIndex: applyIndex(index, outputDevices),
    );
  }

  static void applySelection({
    required NativeAudioDevice input,
    required NativeAudioDevice output,
  }) {
    if (!isSupported) {
      return;
    }
    applyDevice(AudioDeviceBindings.open(), input: input, output: output);
  }

  static Future<void> persistAndApplySelection({
    required AudioDeviceStore store,
    required NativeAudioDevice input,
    required NativeAudioDevice output,
  }) async {
    await store.setInputDevice(
      index: input.index,
      platformId: input.platformId,
    );
    await store.setOutputDevice(
      index: output.index,
      platformId: output.platformId,
    );
    applySelection(input: input, output: output);
  }
}
