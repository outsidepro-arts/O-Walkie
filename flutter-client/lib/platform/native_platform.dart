import 'dart:io' show Platform;

import 'package:flutter/services.dart';

/// Mobile platform hooks (microphone permission, voice communication audio mode).
abstract final class NativePlatform {
  static const _channel =
      MethodChannel('ru.outsidepro_arts.owalkie.flutter/platform');

  static bool get isMobile => Platform.isAndroid || Platform.isIOS;

  static Future<bool> hasMicrophonePermission() async {
    if (!isMobile) {
      return true;
    }
    final granted = await _channel.invokeMethod<bool>('hasMicrophonePermission');
    return granted ?? false;
  }

  static Future<bool> requestMicrophonePermission() async {
    if (!isMobile) {
      return true;
    }
    final granted =
        await _channel.invokeMethod<bool>('requestMicrophonePermission');
    return granted ?? false;
  }

  static Future<bool> ensureMicrophonePermission() async {
    if (await hasMicrophonePermission()) {
      return true;
    }
    return requestMicrophonePermission();
  }

  static Future<void> prepareAudioSession() async {
    if (!isMobile) {
      return;
    }
    await _channel.invokeMethod<void>('prepareAudioSession');
  }

  static Future<void> releaseAudioSession() async {
    if (!isMobile) {
      return;
    }
    await _channel.invokeMethod<void>('releaseAudioSession');
  }
}
