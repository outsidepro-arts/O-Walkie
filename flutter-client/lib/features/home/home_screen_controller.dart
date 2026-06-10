import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:owalkie_core/owalkie_core.dart';

import '../../l10n/app_strings.dart';
import '../../platform/native_platform.dart';
import 'home_screen_state.dart';

final homeScreenControllerProvider =
    NotifierProvider<HomeScreenController, HomeScreenState>(
  HomeScreenController.new,
);

class HomeScreenController extends Notifier<HomeScreenState> {
  SessionService? _session;
  StreamSubscription<SessionWorkerMessage>? _sessionSub;

  @override
  HomeScreenState build() {
    ref.onDispose(_dispose);
    if (Platform.environment['FLUTTER_TEST'] != 'true') {
      unawaited(_ensureSession());
    }
    return const HomeScreenState();
  }

  Future<void> _ensureSession() async {
    if (_session != null) {
      return;
    }
    if (NativePlatform.isMobile) {
      final micOk = await NativePlatform.ensureMicrophonePermission();
      if (!micOk) {
        state = state.copyWith(
          lastError: 'Microphone permission is required for push-to-talk.',
        );
      }
    }
    final service = SessionService();
    try {
      await service.start();
      _session = service;
      _sessionSub = service.messages.listen(_onSessionMessage);
      service.setRxVolumePercent(state.rxVolumePercent);
    } catch (e) {
      state = state.copyWith(
        sessionSupported: false,
        lastError: e.toString(),
        connectionChip: AppStrings.connectionStateUnsupported,
      );
    }
  }

  void _onSessionMessage(SessionWorkerMessage message) {
    switch (message) {
      case SessionCoreInfoMessage(:final version, :final protocolVersion):
        state = state.copyWith(
          coreVersion: version,
          protocolVersion: protocolVersion,
          sessionSupported: true,
          clearError: true,
        );
      case SessionLoadFailedMessage(:final message):
        state = state.copyWith(
          sessionSupported: false,
          lastError: message,
          connectionChip: AppStrings.connectionStateUnsupported,
        );
      case SessionTransportStateMessage(
          :final connected,
          :final connecting,
          :final error,
        ):
        if (connected && NativePlatform.isMobile) {
          unawaited(NativePlatform.prepareAudioSession());
        } else if (!connected && !connecting && NativePlatform.isMobile) {
          unawaited(NativePlatform.releaseAudioSession());
        }
        state = state.copyWith(
          isConnected: connected,
          isConnecting: connecting,
          connectionChip: _chipForTransport(connected, connecting),
          lastError: error,
          clearError: error == null,
        );
      case SessionPttResultMessage(:final active, :final resultCode):
        state = state.copyWith(
          txActive: active,
          lastError: resultCode != 0 ? _pttError(resultCode) : state.lastError,
        );
      case SessionNativeEventMessage(:final eventType, :final info):
        if (info.isNotEmpty) {
          state = state.copyWith(lastError: info);
        }
        if (eventType == OwalkieEventType.rxBroadcastStart) {
          state = state.copyWith(signalChip: 'RX active');
        } else if (eventType == OwalkieEventType.rxBroadcastEnd) {
          state = state.copyWith(signalChip: AppStrings.signalQualityDefault);
        }
      case SessionUnsupportedMessage():
        state = state.copyWith(
          sessionSupported: false,
          connectionChip: AppStrings.connectionStateUnsupported,
        );
    }
  }

  String _chipForTransport(bool connected, bool connecting) {
    if (connected) {
      return AppStrings.connectionStateConnected;
    }
    if (connecting) {
      return AppStrings.connectionStateConnecting;
    }
    return AppStrings.connectionStateDisconnected;
  }

  void toggleConnectionDetails() {
    state = state.copyWith(
      connectionDetailsExpanded: !state.connectionDetailsExpanded,
    );
  }

  void setRxVolume(int percent) {
    final value = percent.clamp(0, 200);
    state = state.copyWith(rxVolumePercent: value);
    _session?.setRxVolumePercent(value);
  }

  void toggleScan() {
    state = state.copyWith(scanActive: !state.scanActive);
  }

  void updateProfile({
    String? name,
    String? host,
    String? portText,
    String? channel,
  }) {
    final parsedPort = int.tryParse(portText ?? '') ?? state.profile.port;
    state = state.copyWith(
      profile: state.profile.copyWith(
        name: name ?? state.profile.name,
        host: host ?? state.profile.host,
        port: parsedPort,
        channel: channel ?? state.profile.channel,
      ),
    );
  }

  Future<void> toggleConnection() async {
    await _ensureSession();
    final session = _session;
    if (session == null || !state.sessionSupported) {
      return;
    }
    if (state.isConnected || state.isConnecting) {
      session.disconnect();
      return;
    }
    final p = state.profile;
    if (p.host.trim().isEmpty) {
      state = state.copyWith(
        lastError: 'Enter server host (LAN IP of the relay, not 127.0.0.1 on a phone).',
      );
      return;
    }
    session.connect(host: p.host.trim(), port: p.port, channel: p.channel, repeater: p.repeater);
  }

  String _pttError(int code) {
    return switch (code) {
      6 => 'Microphone capture failed. Check permission and try again.',
      9 => 'Not connected yet — wait for Connected status.',
      _ => 'Push-to-talk failed (error $code).',
    };
  }

  void pttDown() {
    if (!state.isConnected || state.txActive) {
      return;
    }
    if (NativePlatform.isMobile) {
      unawaited(_pttDownAsync());
      return;
    }
    _session?.pttDown();
  }

  Future<void> _pttDownAsync() async {
    final micOk = await NativePlatform.ensureMicrophonePermission();
    if (!micOk) {
      state = state.copyWith(
        lastError: 'Microphone permission is required for push-to-talk.',
      );
      return;
    }
    _session?.pttDown();
  }

  void pttUp() {
    if (!state.txActive) {
      return;
    }
    _session?.pttUp();
  }

  void _dispose() {
    _sessionSub?.cancel();
    _session?.dispose();
    _session = null;
  }
}
