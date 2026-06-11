import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_core/owalkie_core.dart';
import 'dart:ffi' as ffi;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('Windows FFI lists at least one capture device', () {
    if (!Platform.isWindows) {
      return;
    }
    expect(AudioDeviceBindings.isPlatformSupported, isTrue);

    final builtDll = File(
      r'D:\progworkspace\Vibecoding\O-Walkie\flutter-client\build\windows\x64\runner\Release\owalkie_core.dll',
    );
    if (!builtDll.existsSync()) {
      fail('Build owalkie_core.dll first: ${builtDll.path}');
    }

    final exeDir = File(Platform.resolvedExecutable).parent.path;
    debugPrint('resolvedExecutable=$exeDir');
    debugPrint('loading ${builtDll.path}');

    Object? openError;
    AudioDeviceBindings? bindings;
    try {
      bindings = AudioDeviceBindings.openForLibrary(
        ffi.DynamicLibrary.open(builtDll.path),
      );
    } catch (e) {
      openError = e;
    }
    expect(openError, isNull, reason: 'AudioDeviceBindings.open failed: $openError');

    final inputs = bindings!.listCaptureDevices();
    final outputs = bindings.listPlaybackDevices();
    debugPrint('capture=${inputs.length} playback=${outputs.length}');
    for (final d in inputs) {
      debugPrint('  in[${d.index}]: ${d.name} default=${d.isDefault}');
    }
    for (final d in outputs) {
      debugPrint('  out[${d.index}]: ${d.name} default=${d.isDefault}');
    }

    expect(inputs, isNotEmpty, reason: 'No capture devices from miniaudio');
    expect(outputs, isNotEmpty, reason: 'No playback devices from miniaudio');
  });
}
