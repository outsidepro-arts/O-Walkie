import 'dart:io' show Platform;

import 'package:flutter/services.dart';

import '../../domain/audio_output_profile.dart';
import '../../domain/microphone_source_option.dart';

/// Mobile platform hooks (microphone permission, voice communication audio mode).
abstract final class NativePlatform {
  static const _channel =
      MethodChannel('ru.outsidepro_arts.owalkie.flutter/platform');
  static const _eventsChannel =
      EventChannel('ru.outsidepro_arts.owalkie.flutter/platform_events');

  static const notificationDisconnectEvent = 'notification_disconnect';
  static const networkValidatedEvent = 'network_validated';
  static const networkLostEvent = 'network_lost';
  static const mediaPttToggleEvent = 'media_ptt_toggle';
  static const hardwarePttDownEvent = 'hardware_ptt_down';
  static const hardwarePttUpEvent = 'hardware_ptt_up';
  static const hardwarePttBoundEvent = 'hardware_ptt_bound';
  static const externalPttDownEvent = 'external_ptt_down';
  static const externalPttUpEvent = 'external_ptt_up';
  static const externalPttToggleEvent = 'external_ptt_toggle';
  static const externalCallSignalEvent = 'external_call_signal';
  static const externalConnectEvent = 'external_connect';
  static const externalDisconnectEvent = 'external_disconnect';
  static const externalNextConnectionEvent = 'external_next_connection';
  static const externalPreviousConnectionEvent = 'external_previous_connection';
  static const phoneCallBeginEvent = 'phone_call_begin';
  static const phoneCallEndEvent = 'phone_call_end';

  static const signalWifi = 0;
  static const signalCell = 1;

  static bool get isMobile => Platform.isAndroid || Platform.isIOS;

  static bool get isAndroid => Platform.isAndroid;

  static bool get isWindows => Platform.isWindows;

  static bool get isDesktop =>
      isWindows || Platform.isLinux || Platform.isMacOS;

  /// Android/iOS only. Desktop uses platform-specific channels (e.g. Windows global PTT).
  static Stream<String> get platformEvents {
    if (!isMobile) {
      return const Stream<String>.empty();
    }
    return _eventsChannel
        .receiveBroadcastStream()
        .map((event) => event.toString());
  }

  static Future<bool> hasMicrophonePermission() async {
    if (!isMobile) {
      return true;
    }
    try {
      final granted = await _channel.invokeMethod<bool>('hasMicrophonePermission');
      return granted ?? false;
    } catch (e) {
      return false;
    }
  }

  static Future<bool> requestMicrophonePermission() async {
    if (!isMobile) {
      return true;
    }
    try {
      final granted =
          await _channel.invokeMethod<bool>('requestMicrophonePermission');
      return granted ?? false;
    } catch (e) {
      return false;
    }
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
    try {
      final granted =
          await _channel.invokeMethod<bool>('hasNotificationPermission');
      return granted ?? false;
    } catch (e) {
      return false;
    }
  }

  static Future<bool> requestNotificationPermission() async {
    if (!isAndroid) {
      return true;
    }
    try {
      final granted =
          await _channel.invokeMethod<bool>('requestNotificationPermission');
      return granted ?? false;
    } catch (e) {
      return false;
    }
  }

  static Future<bool> ensureNotificationPermission() async {
    if (await hasNotificationPermission()) {
      return true;
    }
    return requestNotificationPermission();
  }

  static Future<void> prepareAudioSession({
    bool bluetoothHeadset = false,
    String? microphoneProfileId,
  }) async {
    if (!isMobile) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('prepareAudioSession', {
        'bluetoothHeadset': bluetoothHeadset,
        if (microphoneProfileId != null) 'microphoneProfileId': microphoneProfileId,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> applyMicrophoneProfile(
    String profileId,
  ) async {
    if (!isIOS) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('applyMicrophoneProfile', {
        'profileId': profileId,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> applyAudioOutputProfile({
    required String profileId,
    int? audioManagerMode,
    bool? enableBtSco,
    String? iosMode,
    bool? iosDefaultToSpeaker,
    bool? iosAllowBluetoothA2dp,
    bool? iosDuckOthers,
  }) async {
    if (!isMobile) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('applyAudioOutputProfile', {
        'profileId': profileId,
        if (audioManagerMode != null) 'audioManagerMode': audioManagerMode,
        if (enableBtSco != null) 'enableBtSco': enableBtSco,
        if (iosMode != null) 'iosMode': iosMode,
        if (iosDefaultToSpeaker != null)
          'iosDefaultToSpeaker': iosDefaultToSpeaker,
        if (iosAllowBluetoothA2dp != null)
          'iosAllowBluetoothA2dp': iosAllowBluetoothA2dp,
        if (iosDuckOthers != null) 'iosDuckOthers': iosDuckOthers,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<List<AudioOutputProfile>> listAudioOutputProfiles() async {
    if (!isMobile) {
      return const [];
    }
    try {
      final raw = await _channel.invokeListMethod<Map<Object?, Object?>>(
        'listAudioOutputProfiles',
      );
      if (raw == null || raw.isEmpty) {
        return AudioOutputProfile.all;
      }
      return [
        for (final entry in raw)
          AudioOutputProfile.byId(entry['id'] as String? ?? 'media'),
      ];
    } catch (e) {
      return AudioOutputProfile.all;
    }
  }

  static Future<void> releaseAudioSession() async {
    if (!isMobile) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('releaseAudioSession');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> startSessionForeground({required bool connected}) async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('startSessionForeground', {
        'connected': connected,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> updateSessionForeground({required bool connected}) async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('updateSessionForeground', {
        'connected': connected,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> stopSessionForeground() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('stopSessionForeground');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> requestAppExit() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('requestAppExit');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> moveTaskToBack() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('moveTaskToBack');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> startSessionNetworkMonitoring() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('startSessionNetworkMonitoring');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> stopSessionNetworkMonitoring() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('stopSessionNetworkMonitoring');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> openBatterySettings() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('openBatterySettings');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> syncPttMediaSession({required bool active}) async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('syncPttMediaSession', {
        'active': active,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<HardwarePttBinding> getHardwarePttBinding() async {
    if (!isAndroid) {
      return const HardwarePttBinding.unassigned();
    }
    try {
      final map = await _channel.invokeMapMethod<String, dynamic>(
        'getHardwarePttBinding',
      );
      if (map == null) {
        return const HardwarePttBinding.unassigned();
      }
      return HardwarePttBinding(
        keyCode: map['keyCode'] as int? ?? 0,
        scanCode: map['scanCode'] as int? ?? 0,
        assigned: map['assigned'] as bool? ?? false,
      );
    } catch (e) {
      return const HardwarePttBinding.unassigned();
    }
  }

  static Future<void> clearHardwarePttBinding() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('clearHardwarePttBinding');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> startCaptureHardwarePttKey() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('startCaptureHardwarePttKey');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> cancelCaptureHardwarePttKey() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('cancelCaptureHardwarePttKey');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<bool> getExternalControlEnabled() async {
    if (!isAndroid) {
      return false;
    }
    try {
      final enabled = await _channel.invokeMethod<bool>('getExternalControlEnabled');
      return enabled ?? false;
    } catch (e) {
      return false;
    }
  }

  static Future<void> setExternalControlEnabled(bool enabled) async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('setExternalControlEnabled', {
        'enabled': enabled,
      });
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<bool> hasPhoneStatePermission() async {
    if (!isAndroid) {
      return false;
    }
    try {
      final granted = await _channel.invokeMethod<bool>('hasPhoneStatePermission');
      return granted ?? false;
    } catch (e) {
      return false;
    }
  }

  static Future<bool> requestPhoneStatePermission() async {
    if (!isAndroid) {
      return false;
    }
    try {
      final granted =
          await _channel.invokeMethod<bool>('requestPhoneStatePermission');
      return granted ?? false;
    } catch (e) {
      return false;
    }
  }

  static Future<bool> ensurePhoneStatePermission() async {
    if (await hasPhoneStatePermission()) {
      return true;
    }
    return requestPhoneStatePermission();
  }

  static Future<void> startPhoneCallObserver() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('startPhoneCallObserver');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static Future<void> stopPhoneCallObserver() async {
    if (!isAndroid) {
      return;
    }
    try {
      await _channel.invokeMethod<void>('stopPhoneCallObserver');
    } catch (e) {
      // Silently ignore platform errors
    }
  }

  static bool get isIOS => Platform.isIOS;

  static Future<List<MicrophoneSourceOption>> listMicrophoneSources() async {
    if (!isAndroid && !isIOS) {
      return const [];
    }
    try {
      final raw = await _channel.invokeListMethod<Map<Object?, Object?>>(
        'listMicrophoneSources',
      );
      if (raw == null || raw.isEmpty) {
        return const [];
      }
      return [
        for (final entry in raw)
          MicrophoneSourceOption(
            id: entry['id'] as String? ?? '',
            title: entry['title'] as String? ?? '',
            inputPreset: entry['inputPreset'] as int? ?? 1,
          ),
      ];
    } catch (e) {
      return const [];
    }
  }
}

class HardwarePttBinding {
  const HardwarePttBinding({
    required this.keyCode,
    required this.scanCode,
    required this.assigned,
  });

  const HardwarePttBinding.unassigned()
      : keyCode = 0,
        scanCode = 0,
        assigned = false;

  final int keyCode;
  final int scanCode;
  final bool assigned;
}
