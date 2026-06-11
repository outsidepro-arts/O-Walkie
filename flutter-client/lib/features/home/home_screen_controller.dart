import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:owalkie_core/owalkie_core.dart';

import '../../data/server_store.dart';
import '../../domain/profile_save.dart';
import '../../domain/ptt_burst_guard.dart';
import '../../domain/server_profile.dart';
import '../../l10n/app_strings.dart';
import '../../platform/haptics.dart';
import '../../platform/native_platform.dart';
import '../../platform/screen_wake.dart';
import 'home_screen_state.dart';
import 'session_event_mapper.dart';

final homeScreenControllerProvider =
    NotifierProvider<HomeScreenController, HomeScreenState>(
  HomeScreenController.new,
);

class HomeScreenController extends Notifier<HomeScreenState> {
  SessionService? _session;
  StreamSubscription<SessionWorkerMessage>? _sessionSub;
  final PttBurstGuard _pttBurstGuard = PttBurstGuard();
  Timer? _txCountdownTimer;
  bool _parallelTxVibrating = false;

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
    final profile = list[index];
    state = state.copyWith(
      profiles: list,
      selectedServerIndex: index,
      draftProfile: profile,
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
      draftProfile: state.profiles[index],
      clearError: true,
      clearStatusMessage: true,
    );
    unawaited(_store.setLastSelectedName(state.draftProfile.name));
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
    final validation = validateProfileDraft(state.draftProfile);
    if (validation != null) {
      state = state.copyWith(lastError: validation, clearStatusMessage: true);
      return;
    }
    final result = applyProfileSave(
      profiles: state.profiles,
      draft: state.draftProfile,
    );
    await _persistProfiles(result.profiles);
    state = state.copyWith(
      profiles: result.profiles,
      selectedServerIndex: result.selectedIndex,
      draftProfile: result.profiles[result.selectedIndex],
      statusMessage: AppStrings.profileSaved,
      clearError: true,
      clearStatusMessage: false,
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
      draftProfile: list[newIndex],
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
    final prev = state;
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
        if (!connected && !connecting) {
          _pttBurstGuard.reset();
          _cancelTxCountdown();
        }
        state = state.copyWith(
          isConnected: connected,
          isConnecting: connecting,
          isReconnecting: reconnecting,
          txActive: connected ? state.txActive : false,
          isReceivingBroadcast: connected ? state.isReceivingBroadcast : false,
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
        if (!active) {
          _cancelTxCountdown();
        }
      case SessionNativeEventMessage(:final eventType, :final info):
        state = applyNativeSessionEvent(
          state,
          eventType: eventType,
          info: info,
        );
        if (eventType == OwalkieEventType.txCountdownStart) {
          _startTxCountdown(int.tryParse(info) ?? 0);
        } else if (eventType == OwalkieEventType.txStop ||
            eventType == OwalkieEventType.disconnected ||
            eventType == OwalkieEventType.connectionFailed ||
            eventType == OwalkieEventType.protocolError) {
          _cancelTxCountdown();
        }
        if (eventType == OwalkieEventType.disconnected ||
            eventType == OwalkieEventType.connectionFailed ||
            eventType == OwalkieEventType.protocolError) {
          _pttBurstGuard.reset();
        }
      case SessionUnsupportedMessage():
        state = state.copyWith(
          sessionSupported: false,
          connectionChip: AppStrings.connectionStateUnsupported,
        );
      case SessionChannelActivityResultMessage():
        break;
    }
    _syncSideEffects(prev);
  }

  void _syncSideEffects(HomeScreenState prev) {
    final parallel = state.parallelTxActive;
    if (parallel != _parallelTxVibrating) {
      _parallelTxVibrating = parallel;
      unawaited(Haptics.parallelTxCollision(active: parallel));
    }
    if (prev.txActive != state.txActive) {
      unawaited(ScreenWake.setTransmitting(state.txActive));
    }
  }

  void _startTxCountdown(int sec) {
    _txCountdownTimer?.cancel();
    if (sec <= 0) {
      return;
    }
    state = state.copyWith(txCountdownSec: sec);
    unawaited(Haptics.transmitTimeoutPulse());
    var remaining = sec;
    _txCountdownTimer = Timer.periodic(const Duration(seconds: 1), (timer) {
      remaining--;
      if (remaining <= 0 || !state.txActive) {
        timer.cancel();
        _txCountdownTimer = null;
        if (state.txCountdownSec != 0) {
          state = state.copyWith(txCountdownSec: 0);
        }
        return;
      }
      state = state.copyWith(txCountdownSec: remaining);
      if (state.txActive) {
        unawaited(Haptics.transmitTimeoutPulse());
      }
    });
  }

  void _cancelTxCountdown() {
    _txCountdownTimer?.cancel();
    _txCountdownTimer = null;
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
    state = state.copyWith(
      draftProfile: state.draftProfile.copyWith(repeater: enabled),
    );
    _session?.setRepeaterMode(enabled);
  }

  void updateProfile({
    String? name,
    String? host,
    String? portText,
    String? channel,
  }) {
    final parsedPort = int.tryParse(portText ?? '') ?? state.draftProfile.port;
    state = state.copyWith(
      draftProfile: state.draftProfile.copyWith(
        name: name ?? state.draftProfile.name,
        host: host ?? state.draftProfile.host,
        port: parsedPort,
        channel: channel ?? state.draftProfile.channel,
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
    if (!_pttBurstGuard.onPressAttempt()) {
      return;
    }
    state = state.copyWith(isReceivingBroadcast: false);
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
    _pttBurstGuard.onRelease();
  }

  void _dispose() {
    _cancelTxCountdown();
    _pttBurstGuard.reset();
    unawaited(Haptics.parallelTxCollision(active: false));
    unawaited(ScreenWake.setTransmitting(false));
    _sessionSub?.cancel();
    _session?.dispose();
    _session = null;
  }
}
