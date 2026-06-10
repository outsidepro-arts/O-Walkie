// Bindings for owalkie_flutter_bridge.h (regenerate via ffigen).
// ignore_for_file: always_specify_types

import 'dart:ffi' as ffi;

final class OwalkieCoreBindings {
  OwalkieCoreBindings(ffi.DynamicLibrary library)
      : _lookup = library.lookup;

  final ffi.Pointer<T> Function<T extends ffi.NativeType>(String symbolName)
      _lookup;

  late final ffi.Pointer<ffi.Char> Function() _coreVersion = _lookup<
          ffi.NativeFunction<ffi.Pointer<ffi.Char> Function()>>(
        'owalkie_flutter_core_version',
      ).asFunction();

  late final int Function() _protocolVersion = _lookup<
          ffi.NativeFunction<ffi.Int32 Function()>>(
        'owalkie_flutter_protocol_version',
      ).asFunction();

  ffi.Pointer<ffi.Char> owalkie_flutter_core_version() => _coreVersion();

  int owalkie_flutter_protocol_version() => _protocolVersion();
}
