import 'dart:ffi' as ffi;
import 'dart:io';

/// Opens the owalkie_core native library for the current platform.
ffi.DynamicLibrary openOwalkieCoreLibrary() {
  const root = 'owalkie_core';
  if (Platform.isAndroid) {
    return ffi.DynamicLibrary.open('lib$root.so');
  }
  if (Platform.isIOS) {
    // Statically linked via CocoaPods (force_load libowalkie_core.a).
    return ffi.DynamicLibrary.process();
  }
  if (Platform.isWindows) {
    final exeDir = File(Platform.resolvedExecutable).parent.path;
    final nextToExe = '$exeDir\\$root.dll';
    if (File(nextToExe).existsSync()) {
      return ffi.DynamicLibrary.open(nextToExe);
    }
    return ffi.DynamicLibrary.open('$root.dll');
  }
  throw UnsupportedError('owalkie_core FFI unsupported on this platform');
}
