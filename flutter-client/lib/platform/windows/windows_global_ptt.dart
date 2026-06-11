import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../../domain/windows_ptt_binding.dart';

/// Native WH_KEYBOARD_LL global PTT (hold/release) on Windows.
abstract final class WindowsGlobalPtt {
  static const _channel =
      MethodChannel('ru.outsidepro_arts.owalkie.flutter/windows_global_ptt');
  static const _eventsChannel =
      EventChannel('ru.outsidepro_arts.owalkie.flutter/windows_global_ptt_events');

  static const downEvent = 'global_ptt_down';
  static const upEvent = 'global_ptt_up';
  static const capturedEvent = 'global_ptt_captured';

  static bool get isSupported =>
      Platform.isWindows && !kIsWeb && Platform.environment['FLUTTER_TEST'] != 'true';

  static Stream<String> get events =>
      _eventsChannel.receiveBroadcastStream().map((event) => event.toString());

  static Future<void> installHook() async {
    if (!isSupported) {
      return;
    }
    await _channel.invokeMethod<void>('installHook');
  }

  static Future<void> uninstallHook() async {
    if (!isSupported) {
      return;
    }
    await _channel.invokeMethod<void>('uninstallHook');
  }

  static Future<void> setBinding(WindowsPttBinding binding) async {
    if (!isSupported) {
      return;
    }
    await _channel.invokeMethod<void>('setBinding', {
      'vkey': binding.vkey,
      'mods': binding.mods,
    });
  }

  static Future<void> clearBinding() async {
    if (!isSupported) {
      return;
    }
    await _channel.invokeMethod<void>('clearBinding');
  }

  static Future<WindowsPttBinding?> getBinding() async {
    if (!isSupported) {
      return null;
    }
    final value = await _channel.invokeMethod<dynamic>('getBinding');
    if (value is! Map) {
      return null;
    }
    return WindowsPttBinding.fromPlatformMap(value);
  }

  static Future<bool> startCapture() async {
    if (!isSupported) {
      return false;
    }
    final ok = await _channel.invokeMethod<bool>('startCapture');
    return ok ?? false;
  }

  static Future<void> cancelCapture() async {
    if (!isSupported) {
      return;
    }
    await _channel.invokeMethod<void>('cancelCapture');
  }

  static Future<WindowsPttBinding?> takeCaptureResult() async {
    if (!isSupported) {
      return null;
    }
    final value = await _channel.invokeMethod<dynamic>('takeCaptureResult');
    if (value is! Map) {
      return null;
    }
    return WindowsPttBinding.fromPlatformMap(value);
  }
}
