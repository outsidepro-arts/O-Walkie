import 'package:ffi/ffi.dart';

import 'owalkie_core_bindings_generated.dart';
import 'src/native_library.dart';

export 'session_service.dart';
export 'src/audio_device_bindings.dart'
    show AudioDeviceBindings, NativeAudioDevice;
export 'src/link_signal.dart';
export 'src/session_event_type.dart';
export 'src/session_messages.dart';
export 'ui_sound_library.dart';

/// Thin Dart facade over the O-Walkie native core (FFI plugin).
///
/// Session transport callbacks will be wired here in a later phase; for now
/// exposes version probes used by the shell UI.
class OwalkieCore {
  OwalkieCore._(this._bindings);

  static OwalkieCore? _instance;

  final OwalkieCoreBindings _bindings;

  static OwalkieCore get instance {
    return _instance ??= OwalkieCore._(_loadBindings());
  }

  static OwalkieCoreBindings _loadBindings() {
    return OwalkieCoreBindings(openOwalkieCoreLibrary());
  }

  String get versionString {
    final ptr = _bindings.owalkie_flutter_core_version();
    return ptr.cast<Utf8>().toDartString();
  }

  int get protocolVersion => _bindings.owalkie_flutter_protocol_version();
}
