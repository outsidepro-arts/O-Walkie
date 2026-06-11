import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:owalkie_core/owalkie_core.dart';

import '../../data/server_store.dart';
import '../../domain/server_profile.dart';
import '../../l10n/app_strings.dart';
import '../../platform/native_platform.dart';
import 'home_screen_state.dart';
import 'session_event_mapper.dart';

final homeScreenControllerProvider =
    NotifierProvider<HomeScreenController, HomeScreenState>(
  HomeScreenController.new,
);

class HomeScreenController extends Notifier<HomeScreenState> {
  SessionService? _session;
  StreamSubscription<SessionWorkerMessage>? _sessionSub;

  ServerStore get _store => ref.read(serverStoreProvider);

  @override
  HomeScreenState build() {
    ref.onDispose(_dispose);
    if (Platform.environment['FLUTTER_TEST'] != 'true') {
      unawaited(_bootstrap());
    }
    return const HomeScreenState();
  }

  Future<void> _bootstrap() async {
    await _loadProfiles();
    await _ensureSession();
  }

  Future<void> _loadProfiles() async {
    var list = await _store.load();
    if (list.isEmpty) {
      list = [const ServerProfile()];
      await _store.save(list);
    }
    var index = 0;
    final selectedName = await _store.getLastSelectedName();
    if (selectedName != null) {
      final found = list.indexWhere((p) => p.name == selectedName);
      if (found >= 0) {
        index = found;
      }
    }
    state = state.copyWith(
      profiles: list,
      selectedServerIndex: index,
      clearError: true,
    );
  }

  Future<void> _persistProfiles(List<ServerProfile> profiles) async {
    await _store.save(profiles);
    final index = state.selectedServerIndex.clamp(0, profiles.length - 1);
    if (profiles.isNotEmpty) {
      await _store.setLastSelectedName(profiles[index].name);
    }
  }

  bool _rejectIfConnected() {
    if (state.isConnected || state.isConnecting) {
      state = state.copyWith(
        lastError: AppStrings.cannotSwitchProfileConnected,
      );
      return true;
    }
    return false;
  }

  void selectProfile(int index) {
    if (_rejectIfConnected()) {
      return;
    }
    if (index < 0 || index >= state.profiles.length) {
      return;
    }
    state = state.copyWith(
      selectedServerIndex: index,
      clearError: true,
      clearStatusMessage: true,
    );
    unawaited(_store.setLastSelectedName(state.profile.name));
  }

  void previousProfile() {
    if (!state.canSwitchProfiles) {
      if (_rejectIfConnected()) {
        return;
      }
      return;
    }
    final next = (state.selectedServerIndex - 1 + state.profiles.length) %
        state.profiles.length;
    selectProfile(next);
  }

  void nextProfile() {
    if (!state.canSwitchProfiles) {
      if (_rejectIfConnected()) {
        return;
      }
      return;
    }
    final next = (state.selectedServerIndex + 1) % state.profiles.length;
    selectProfile(next);
  }

  Future<void> saveCurrentProfile() async {
    final index = state.selectedServerIndex;
    if (index < 0 || index >= state.profiles.length) {
      return;
    }
    final list = [...state.profiles];
    list[index] = state.profile;
    await _persistProfiles(list);
    state = state.copyWith(
      profiles: list,
      statusMessage: AppStrings.profileSaved,
      clearError: true,
    );
  }

  Future<void> deleteCurrentProfile() async {
    if (_rejectIfConnected()) {
      return;
    }
    if (state.profiles.length <= 1) {
      state = state.copyWith(lastError: AppStrings.cannotDeleteLastProfile);
      return;
    }
    final index = state.selectedServerIndex;
    final list = [...state.profiles]..removeAt(index);
    final newIndex = index.clamp(0, list.length - 1);
    await _persistProfiles(list);
    state = state.copyWith(
      profiles: list,
      selectedServerIndex: newIndex,
      clearError: true,
      clearStatusMessage: true,
    );
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
          :final reconnecting,
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
          isReconnecting: reconnecting,
          connectionChip: connectionChipForTransport(
            connected: connected,
            connecting: connecting,
            reconnecting: reconnecting,
          ),
          lastError: error,
          clearError: error == null,
          statusInfo: reconnecting ? AppStrings.connectionStateReconnecting : state.statusInfo,
        );
      case SessionPttResultMessage(:final active, :final resultCode):
        state = state.copyWith(
          txActive: active,
          lastError: resultCode != 0 ? _pttError(resultCode) : state.lastError,
        );
      case SessionNativeEventMessage(:final eventType, :final info):
        state = applyNativeSessionEvent(
          state,
          eventType: eventType,
          info: info,
        );
      case SessionUnsupportedMessage():
        state = state.copyWith(
          sessionSupported: false,
          connectionChip: AppStrings.connectionStateUnsupported,
        );
      case SessionChannelActivityResultMessage():
        break;
    }
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

  void setRepeaterMode(bool enabled) {
    state = state.withProfileAt(
      state.selectedServerIndex,
      state.profile.copyWith(repeater: enabled),
    );
    _session?.setRepeaterMode(enabled);
  }

  void updateProfile({
    String? name,
    String? host,
    String? portText,
    String? channel,
  }) {
    final parsedPort = int.tryParse(portText ?? '') ?? state.profile.port;
    final updated = state.profile.copyWith(
      name: name ?? state.profile.name,
      host: host ?? state.profile.host,
      port: parsedPort,
      channel: channel ?? state.profile.channel,
    );
    state = state.withProfileAt(state.selectedServerIndex, updated);
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
        lastError:
            'Enter server host (LAN IP of the relay, not 127.0.0.1 on a phone).',
      );
      return;
    }
    session.connect(
      host: p.host.trim(),
      port: p.port,
      channel: p.channel,
      repeater: p.repeater,
    );
  }

  String _pttError(int code) {
    return switch (code) {
      6 => 'Microphone capture failed. Check permission and try again.',
      9 => 'Not connected yet — wait for Connected status.',
      _ => 'Push-to-talk failed (error $code).',
    };
  }

  void pttDown() {
    if (!pttEnabled(
      sessionSupported: state.sessionSupported,
      isConnected: state.isConnected,
      pttServerLocked: state.pttServerLocked,
    )) {
      return;
    }
    if (state.txActive) {
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
