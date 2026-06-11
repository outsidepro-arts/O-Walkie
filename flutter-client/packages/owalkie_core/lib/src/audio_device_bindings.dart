import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'native_library.dart';

/// Native audio I/O device entry (mirrors [owalkie_flutter_audio_device_info]).
final class OwAudioDeviceInfo extends ffi.Struct {
  @ffi.Int32()
  external int index;

  @ffi.Int32()
  external int isDefault;

  @ffi.Array(256)
  external ffi.Array<ffi.Char> name;
}

class NativeAudioDevice {
  const NativeAudioDevice({
    required this.index,
    required this.name,
    required this.isDefault,
    this.platformId,
  });

  final int index;
  final String name;
  final bool isDefault;

  /// Android AAudio id from [AudioManager]; null on desktop (use [index]).
  final int? platformId;
}

/// miniaudio device enumeration via owalkie_core FFI (main or worker isolate).
class AudioDeviceBindings {
  AudioDeviceBindings._(this._lib);

  static AudioDeviceBindings? _instance;

  factory AudioDeviceBindings.open() {
    return _instance ??= AudioDeviceBindings._(openOwalkieCoreLibrary());
  }

  factory AudioDeviceBindings.openForLibrary(ffi.DynamicLibrary library) {
    return AudioDeviceBindings._(library);
  }

  final ffi.DynamicLibrary _lib;

  static const _maxDevices = 64;

  late final int Function(ffi.Pointer<OwAudioDeviceInfo>, int)
      _listCapture = _lib.lookupFunction<
          ffi.Int32 Function(ffi.Pointer<OwAudioDeviceInfo>, ffi.Int32),
          int Function(ffi.Pointer<OwAudioDeviceInfo>, int)>(
    'owalkie_flutter_list_capture_devices',
  );

  late final int Function(ffi.Pointer<OwAudioDeviceInfo>, int)
      _listPlayback = _lib.lookupFunction<
          ffi.Int32 Function(ffi.Pointer<OwAudioDeviceInfo>, ffi.Int32),
          int Function(ffi.Pointer<OwAudioDeviceInfo>, int)>(
    'owalkie_flutter_list_playback_devices',
  );

  late final void Function(int) _setCapture = _lib.lookupFunction<
      ffi.Void Function(ffi.Int32),
      void Function(int)>('owalkie_flutter_set_capture_device_index');

  late final void Function(int) _setPlayback = _lib.lookupFunction<
      ffi.Void Function(ffi.Int32),
      void Function(int)>('owalkie_flutter_set_playback_device_index');

  late final void Function(int) _setCapturePlatformId = _lib.lookupFunction<
      ffi.Void Function(ffi.Int32),
      void Function(int)>('owalkie_flutter_set_capture_platform_device_id');

  late final void Function(int) _setCaptureAaudioPreset = _lib.lookupFunction<
      ffi.Void Function(ffi.Int32),
      void Function(int)>('owalkie_flutter_set_capture_aaudio_input_preset');

  late final void Function(int) _setPlaybackPlatformId = _lib.lookupFunction<
      ffi.Void Function(ffi.Int32),
      void Function(int)>('owalkie_flutter_set_playback_platform_device_id');

  late final int Function() _getCapture = _lib.lookupFunction<
      ffi.Int32 Function(),
      int Function()>('owalkie_flutter_get_capture_device_index');

  late final int Function() _getPlayback = _lib.lookupFunction<
      ffi.Int32 Function(),
      int Function()>('owalkie_flutter_get_playback_device_index');

  static bool get isPlatformSupported =>
      Platform.isAndroid || Platform.isWindows || Platform.isIOS;

  List<NativeAudioDevice> listCaptureDevices() =>
      _listDevices(_listCapture);

  List<NativeAudioDevice> listPlaybackDevices() =>
      _listDevices(_listPlayback);

  void setCaptureDeviceIndex(int index) => _setCapture(index);

  void setPlaybackDeviceIndex(int index) => _setPlayback(index);

  void setCapturePlatformDeviceId(int platformId) =>
      _setCapturePlatformId(platformId);

  void setCaptureAaudioInputPreset(int preset) =>
      _setCaptureAaudioPreset(preset);

  void setPlaybackPlatformDeviceId(int platformId) =>
      _setPlaybackPlatformId(platformId);

  int captureDeviceIndex() => _getCapture();

  int playbackDeviceIndex() => _getPlayback();

  void applySelection({
    required int inputIndex,
    required int outputIndex,
    int? inputPlatformId,
    int? outputPlatformId,
  }) {
    if (inputPlatformId != null && inputPlatformId >= 0) {
      setCapturePlatformDeviceId(inputPlatformId);
    } else {
      setCaptureDeviceIndex(inputIndex);
    }
    if (outputPlatformId != null && outputPlatformId >= 0) {
      setPlaybackPlatformDeviceId(outputPlatformId);
    } else {
      setPlaybackDeviceIndex(outputIndex);
    }
  }

  void applyIndices({required int inputIndex, required int outputIndex}) {
    applySelection(inputIndex: inputIndex, outputIndex: outputIndex);
  }

  List<NativeAudioDevice> _listDevices(
    int Function(ffi.Pointer<OwAudioDeviceInfo>, int) listFn,
  ) {
    if (!isPlatformSupported) {
      return const [];
    }
    final buffer = calloc<OwAudioDeviceInfo>(_maxDevices);
    try {
      final count = listFn(buffer, _maxDevices);
      if (count <= 0) {
        return const [];
      }
      return [
        for (var i = 0; i < count; i++)
          NativeAudioDevice(
            index: (buffer + i).ref.index,
            name: _readName((buffer + i).ref.name, (buffer + i).ref.index),
            isDefault: (buffer + i).ref.isDefault != 0,
          ),
      ];
    } finally {
      calloc.free(buffer);
    }
  }

  static String _readName(ffi.Array<ffi.Char> chars, int index) {
    try {
      final bytes = <int>[];
      for (var i = 0; i < 256; i++) {
        final c = chars[i];
        if (c == 0) {
          break;
        }
        // miniaudio names are UTF-8; Char is signed on Windows.
        bytes.add(c & 0xFF);
      }
      if (bytes.isEmpty) {
        return 'Audio device $index';
      }
      return utf8.decode(bytes, allowMalformed: true);
    } catch (_) {
      return 'Audio device $index';
    }
  }
}
