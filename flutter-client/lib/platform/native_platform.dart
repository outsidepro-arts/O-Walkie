import 'dart:io' show Platform;

import 'package:flutter/services.dart';

/// Mobile platform hooks (microphone permission, voice communication audio mode).
abstract final class NativePlatform {
  static const _channel =
      MethodChannel('ru.outsidepro_arts.owalkie.flutter/platform');
  static const _eventsChannel =
      EventChannel('ru.outsidepro_arts.owalkie.flutter/platform_events');

  static const notificationDisconnectEvent = 'notification_disconnect';
  static const networkValidatedEvent = 'network_validated';
  static const networkLostEvent = 'network_lost';

  static const signalWifi = 0;
  static const signalCell = 1;

  static bool get isMobile => Platform.isAndroid || Platform.isIOS;

  static bool get isAndroid => Platform.isAndroid;

  static Stream<String> get platformEvents =>
      _eventsChannel.receiveBroadcastStream().map((event) => event.toString());

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

  static Future<bool> hasNotificationPermission() async {
    if (!isAndroid) {
      return true;
    }
    final granted =
        await _channel.invokeMethod<bool>('hasNotificationPermission');
    return granted ?? false;
  }

  static Future<bool> requestNotificationPermission() async {
    if (!isAndroid) {
      return true;
    }
    final granted =
        await _channel.invokeMethod<bool>('requestNotificationPermission');
    return granted ?? false;
  }

  static Future<bool> ensureNotificationPermission() async {
    if (await hasNotificationPermission()) {
      return true;
    }
    return requestNotificationPermission();
  }

  static Future<void> prepareAudioSession({bool bluetoothHeadset = false}) async {
    if (!isMobile) {
      return;
    }
    await _channel.invokeMethod<void>('prepareAudioSession', {
      'bluetoothHeadset': bluetoothHeadset,
    });
  }

  static Future<void> releaseAudioSession() async {
    if (!isMobile) {
      return;
    }
    await _channel.invokeMethod<void>('releaseAudioSession');
  }

  static Future<void> startSessionForeground({required bool connected}) async {
    if (!isAndroid) {
      return;
    }
    await _channel.invokeMethod<void>('startSessionForeground', {
      'connected': connected,
    });
  }

  static Future<void> updateSessionForeground({required bool connected}) async {
    if (!isAndroid) {
      return;
    }
    await _channel.invokeMethod<void>('updateSessionForeground', {
      'connected': connected,
    });
  }

  static Future<void> stopSessionForeground() async {
    if (!isAndroid) {
      return;
    }
    await _channel.invokeMethod<void>('stopSessionForeground');
  }

  static Future<void> startSessionNetworkMonitoring() async {
    if (!isAndroid) {
      return;
    }
    await _channel.invokeMethod<void>('startSessionNetworkMonitoring');
  }

  static Future<void> stopSessionNetworkMonitoring() async {
    if (!isAndroid) {
      return;
    }
    await _channel.invokeMethod<void>('stopSessionNetworkMonitoring');
  }

  static Future<void> openBatterySettings() async {
    if (!isAndroid) {
      return;
    }
    await _channel.invokeMethod<void>('openBatterySettings');
  }
}
