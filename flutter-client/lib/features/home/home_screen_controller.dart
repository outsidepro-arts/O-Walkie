import 'dart:async';
import 'dart:io' show Platform;

import 'package:app_links/app_links.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:owalkie_core/owalkie_core.dart';
import 'package:share_plus/share_plus.dart';

import '../../data/audio_device_store.dart';
import '../../data/audio_output_profile_store.dart';
import '../../domain/audio_output_profile.dart';
import '../../data/microphone_source_store.dart';
import '../../data/audio_settings_store.dart';
import '../../data/server_store.dart';
import '../../data/signal_pattern_store.dart';
import '../../data/warm_mic_recorder_store.dart';
import '../../domain/connection_link.dart';
import '../../domain/profile_save.dart';
import '../../domain/ptt_burst_guard.dart';
import '../../domain/scan_endpoint.dart';
import '../../domain/scan_mode.dart';
import '../../domain/server_profile.dart';
import '../../domain/signal_pattern.dart';
import '../../domain/signal_point_codec.dart';
import '../../l10n/app_strings.dart';
import '../../platform/audio_device_service.dart';
import '../../platform/audio_interruption_manager.dart';
import '../../platform/audio_output_profile_service.dart';
import '../../data/vibration_imitation_store.dart';
import '../../platform/haptics.dart';
import '../../platform/native_platform.dart';
import '../../platform/session_telemetry.dart';
import '../../platform/signal_preview_player.dart';
import '../../platform/screen_wake.dart';
import '../../platform/ui_signal_player.dart';
import '../../platform/windows/desktop_shell.dart';
import '../../platform/windows/windows_global_ptt.dart';
import 'home_screen_state.dart';
import 'session_event_mapper.dart';

final homeScreenControllerProvider =
    NotifierProvider<HomeScreenController, HomeScreenState>(
  HomeScreenController.new,
);

class HomeScreenController extends Notifier<HomeScreenState> {
  SessionService? _session;
  StreamSubscription<SessionWorkerMessage>? _sessionSub;
  StreamSubscription<String>? _platformSub;
  StreamSubscription<String>? _windowsPttSub;
  final PttBurstGuard _pttBurstGuard = PttBurstGuard();
  Timer? _txCountdownTimer;
  bool _parallelTxVibrating = false;
  bool _sessionForegroundActive = false;
  AudioInterruptionManager? _audioInterruption;
  StreamSubscription<Uri>? _deepLinkSub;
  bool _scanLoopActive = false;
  ScanMode? _scanMode;
  Timer? _rxVolumePreviewTimer;
  bool _userRequestedConnection = false;
  bool _suppressTransientConnectionErrorTone = false;
  bool _skipNextManualDisconnectTone = false;
  bool _appInForeground = true;
  bool _disposed = false;
  bool _pttDownInProgress = false;
  bool _phoneCallPauseInProgress = false;
  Completer<void>? _scanCancellation;
  Future<void>? _sessionTeardownFuture;
  Future<void>? _ensureSessionFuture;
  final SessionTelemetry telemetry = SessionTelemetry();
  static const _scanInterval = Duration(seconds: 10);
  static const _scanQueryTimeoutMs = 4000;
  static const _rxVolumePreviewDelay = Duration(milliseconds: 120);

  ServerStore get _store => ref.read(serverStoreProvider);
  PhoneCallPauseStore get _phoneCallPauseStore =>
      ref.read(phoneCallPauseStoreProvider);
  MediaButtonPttStore get _mediaButtonPttStore =>
      ref.read(mediaButtonPttStoreProvider);
  WarmMicRecorderStore get _warmMicRecorderStore =>
      ref.read(warmMicRecorderStoreProvider);
  RogerPatternStore get _rogerStore => ref.read(rogerPatternStoreProvider);
  CallingPatternStore get _callingStore => ref.read(callingPatternStoreProvider);

  @override
  HomeScreenState build() {
    ref.onDispose(_dispose);
    _pttBurstGuard.onBlockedChanged = _onPttBurstPressBlockedChanged;
    if (Platform.environment['FLUTTER_TEST'] != 'true') {
      if (NativePlatform.isMobile) {
        _platformSub ??= NativePlatform.platformEvents.listen(_onPlatformEvent);
      }
      if (Platform.isWindows) {
        _windowsPttSub ??=
            WindowsGlobalPtt.events.listen(_onWindowsGlobalPttEvent);
      }
      unawaited(_bootstrap());
    }
    return HomeScreenState();
  }

  void _onPttBurstPressBlockedChanged(bool blocked) {
    state = state.copyWith(pttBurstPressBlocked: blocked);
    _stopPttIfUiDisabled();
  }

  void _stopPttIfUiDisabled() {
    if (!pttUiEnabledFor(state) && state.txActive) {
      pttUp();
    }
  }

  void _onWindowsGlobalPttEvent(String event) {
    if (_disposed) return;
    if (event == WindowsGlobalPtt.downEvent) {
      scheduleMicrotask(pttDown);
      return;
    }
    if (event == WindowsGlobalPtt.upEvent) {
      scheduleMicrotask(pttUp);
    }
  }

  void _onPlatformEvent(String event) {
    if (_disposed) return;
    if (event == NativePlatform.notificationDisconnectEvent) {
      unawaited(toggleConnection());
      return;
    }
    if (event == NativePlatform.mediaPttToggleEvent) {
      scheduleMicrotask(togglePttLatch);
      return;
    }
    if (event == NativePlatform.hardwarePttDownEvent) {
      scheduleMicrotask(pttDown);
      return;
    }
    if (event == NativePlatform.hardwarePttUpEvent) {
      scheduleMicrotask(pttUp);
      return;
    }
    if (event == NativePlatform.externalPttDownEvent) {
      scheduleMicrotask(pttDown);
      return;
    }
    if (event == NativePlatform.externalPttUpEvent) {
      scheduleMicrotask(pttUp);
      return;
    }
    if (event == NativePlatform.externalPttToggleEvent) {
      scheduleMicrotask(togglePttLatch);
      return;
    }
    if (event == NativePlatform.externalCallSignalEvent) {
      scheduleMicrotask(sendCall);
      return;
    }
    if (event == NativePlatform.externalConnectEvent) {
      unawaited(externalConnect());
      return;
    }
    if (event == NativePlatform.externalDisconnectEvent) {
      unawaited(externalDisconnect());
      return;
    }
    if (event == NativePlatform.externalNextConnectionEvent) {
      unawaited(externalSwitchProfile(1));
      return;
    }
    if (event == NativePlatform.externalPreviousConnectionEvent) {
      unawaited(externalSwitchProfile(-1));
      return;
    }
    if (event.startsWith('${NativePlatform.networkValidatedEvent}:')) {
      final handleText = event.substring(NativePlatform.networkValidatedEvent.length + 1);
      final handle = int.tryParse(handleText) ?? 0;
      _onNetworkValidated(networkHandle: handle);
      return;
    }
    if (event == NativePlatform.networkValidatedEvent) {
      _onNetworkValidated(networkHandle: 0);
      return;
    }
    if (event.startsWith('signal_report:')) {
      final parts = event.split(':');
      if (parts.length == 3) {
        final mode = int.tryParse(parts[1]);
        final value = int.tryParse(parts[2]);
        if (mode != null && value != null) {
          _session?.reportSignal(mode: mode, value: value);
        }
      }
      return;
    }
    if (event.startsWith('signal_clear:')) {
      final parts = event.split(':');
      if (parts.length == 2) {
        final mode = int.tryParse(parts[1]);
        if (mode != null) {
          _session?.clearSignal(mode);
        }
      }
    }
  }

  Future<void> _releaseMobileAudioAndTeardown() async {
    if (!NativePlatform.isMobile) return;
    await NativePlatform.releaseAudioSession();
    _session?.setAndroidBtVoiceRoute(false);
    await _teardownIdleSession();
  }

  void _onNetworkValidated({required int networkHandle}) {
    if (!state.isConnected && !state.isConnecting && !state.isReconnecting) {
      return;
    }
    _session?.bindProcessNetwork(networkHandle);
    _session?.recoverAfterNetworkHandoff();
  }

  Future<void> _bootstrap() async {
    await _loadProfiles();
    if (NativePlatform.isMobile) {
      final probe = OwalkieCore.probe();
      state = state.copyWith(
        coreVersion: probe.version,
        protocolVersion: probe.protocol,
        sessionSupported: probe.supported,
        connectionChip: probe.supported
            ? state.connectionChip
            : AppStrings.connectionStateUnsupported,
        lastError: probe.supported
            ? state.lastError
            : (probe.version.isEmpty ? 'owalkie_core load failed' : null),
      );
      await UiSignalPlayer.ensureLoaded();
    } else {
      await _ensureSession();
      if (Platform.isWindows) {
        await ref.read(desktopShellProvider).applyStoredBinding();
      }
      _audioInterruption ??= AudioInterruptionManager(
        onInterruptBegin: _pauseRelayForPhoneCall,
        onInterruptEnd: _resumeRelayAfterPhoneCall,
      );
      await _audioInterruption!.start();
    }
    if (Haptics.showsDesktopSettings) {
      Haptics.applyFromStore(ref.read(vibrationImitationStoreProvider));
    }
    await _initDeepLinks();
  }

  Future<void> _initDeepLinks() async {
    if (Platform.environment['FLUTTER_TEST'] == 'true') {
      return;
    }
    final appLinks = AppLinks();
    final initial = await appLinks.getInitialLink();
    if (initial != null) {
      applyConnectionLink(initial);
    }
    _deepLinkSub ??= appLinks.uriLinkStream.listen(applyConnectionLink);
  }

  void applyConnectionLink(Uri uri, {bool announce = true}) {
    final profile = parseConnectionProfileFromUri(uri);
    if (profile == null) {
      if (announce) {
        state = state.copyWith(
          lastError: AppStrings.connectionLinkInvalid,
          clearStatusMessage: true,
        );
      }
      return;
    }
    state = state.copyWith(
      draftProfile: profile,
      statusMessage: announce ? AppStrings.connectionLinkImported : null,
      clearError: true,
      clearStatusMessage: !announce,
    );
    if (state.isConnected || state.isConnecting || state.relayPausedForPhoneCall) {
      unawaited(_reconnectToProfile(profile));
    }
  }

  Future<void> _reconnectToProfile(ServerProfile profile) async {
    final session = _session;
    if (session == null) {
      return;
    }
    await session.disconnect();
    session.connect(
      host: profile.host.trim(),
      port: profile.port,
      channel: profile.channel,
      repeater: profile.repeater,
    );
  }

  String buildShareLink({required bool includeName}) {
    var profile = state.draftProfile;
    if (includeName && profile.name.trim().isEmpty) {
      profile = profile.copyWith(name: 'Connection');
    }
    return buildConnectionDeepLink(profile, includeName: includeName);
  }

  Future<void> shareConnectionLink({required bool includeName}) async {
    final link = buildShareLink(includeName: includeName);
    await Share.share(link, subject: 'O-Walkie connection');
  }

  Future<void> copyConnectionLinkToClipboard({required bool includeName}) async {
    final link = buildShareLink(includeName: includeName);
    await Clipboard.setData(ClipboardData(text: link));
    state = state.copyWith(
      statusMessage: AppStrings.connectionLinkCopied,
      clearError: true,
    );
  }

  Future<void> importConnectionFromClipboard() async {
    final data = await Clipboard.getData(Clipboard.kTextPlain);
    final text = data?.text?.trim() ?? '';
    if (text.isEmpty) {
      state = state.copyWith(
        lastError: AppStrings.connectionLinkInvalid,
        clearStatusMessage: true,
      );
      return;
    }
    final profile = parseConnectionProfileFromLink(text);
    if (profile == null) {
      state = state.copyWith(
        lastError: AppStrings.connectionLinkInvalid,
        clearStatusMessage: true,
      );
      return;
    }
    applyConnectionLink(Uri.parse(extractConnectionLink(text) ?? text));
  }

  Future<void> _applyVoiceAudioRoute() async {
    if (!NativePlatform.isMobile) {
      return;
    }
    final outputProfile = AudioOutputProfileService.resolveStored(
      ref.read(audioOutputProfileStoreProvider),
    );
    final bt = outputProfile.enableBtSco;
    await AudioDeviceService.applyFromStore(
      ref.read(audioDeviceStoreProvider),
      microphoneStore: ref.read(microphoneSourceStoreProvider),
    );
    await NativePlatform.prepareAudioSession(
      bluetoothHeadset: bt,
      microphoneProfileId: ref.read(microphoneSourceStoreProvider).selectedId(),
    );
    await AudioOutputProfileService.applyProfile(outputProfile);
  }

  Future<void> _startMobileAudioStack() async {
    _audioInterruption ??= AudioInterruptionManager(
      onInterruptBegin: _pauseRelayForPhoneCall,
      onInterruptEnd: _resumeRelayAfterPhoneCall,
    );
    await _audioInterruption!.start();
  }

  Future<void> _teardownIdleSession() async {
    if (sessionKeepAlive || _scanLoopActive || _session == null) {
      return;
    }
    _sessionTeardownFuture ??= _stopSessionWorker().whenComplete(() {
      _sessionTeardownFuture = null;
    });
    await _sessionTeardownFuture;
  }

  /// True while the user wants an active relay session (connect / reconnect / phone pause).
  bool get sessionKeepAlive =>
      state.isConnected ||
      state.isConnecting ||
      state.relayPausedForPhoneCall ||
      _userRequestedConnection;

  /// Release idle session resources before desktop app exit.
  Future<void> prepareForAppExit() async {
    stopScanning(announce: false);
    await _teardownIdleSession();
  }

  /// Disconnect and stop the worker (tray Exit, forced shutdown).
  Future<void> shutdownForAppExit() async {
    stopScanning(announce: false);
    if (state.txActive) {
      pttUp();
    }
    _userRequestedConnection = false;
    if (_session != null) {
      await _session!.disconnect();
    }
    if (_sessionForegroundActive) {
      _sessionForegroundActive = false;
      unawaited(NativePlatform.stopSessionForeground());
    }
    await _stopSessionWorker();
  }

  Future<void> _stopSessionWorker() async {
    if (_session == null) {
      return;
    }
    if (NativePlatform.isMobile) {
      await NativePlatform.releaseAudioSession();
      await _audioInterruption?.stop();
      _audioInterruption = null;
    }
    await _sessionSub?.cancel();
    _sessionSub = null;
    await _session?.stop();
    _session = null;
  }

  /// Android back / desktop close: minimize while session is desired; fully exit when idle.
  Future<void> handleSystemBack() async {
    if (sessionKeepAlive) {
      if (NativePlatform.isAndroid) {
        await NativePlatform.moveTaskToBack();
      } else if (Platform.isWindows) {
        await ref.read(desktopShellProvider).hideToTray();
      }
      return;
    }
    stopScanning(announce: false);
    await _teardownIdleSession();
    if (NativePlatform.isAndroid) {
      await NativePlatform.requestAppExit();
    } else if (Platform.isWindows) {
      await ref.read(desktopShellProvider).exitApplication();
    }
  }

  void _pauseRelayForPhoneCall() {
    if (_phoneCallPauseInProgress) {
      return;
    }
    if (!_phoneCallPauseStore.isEnabled()) {
      return;
    }
    if (state.relayPausedForPhoneCall) {
      return;
    }
    if (!state.isConnected && !state.isConnecting) {
      return;
    }
    _phoneCallPauseInProgress = true;
    state = state.copyWith(
      relayPausedForPhoneCall: true,
      isConnected: false,
      isConnecting: false,
      isReconnecting: false,
      txActive: false,
      connectionChip: AppStrings.connectionStatePausedPhoneCall,
    );
    if (NativePlatform.isMobile) {
      unawaited(NativePlatform.releaseAudioSession());
      _session?.setAndroidBtVoiceRoute(false);
    }
    unawaited(_syncPttMediaSession());
    _session?.pauseRelay();
    unawaited(_syncSessionForeground());
    _phoneCallPauseInProgress = false;
  }

  void _resumeRelayAfterPhoneCall() {
    if (_phoneCallPauseInProgress) {
      return;
    }
    if (!state.relayPausedForPhoneCall) {
      return;
    }
    _phoneCallPauseInProgress = true;
    state = state.copyWith(
      relayPausedForPhoneCall: false,
      connectionChip: AppStrings.connectionStateConnecting,
      isConnecting: true,
    );
    unawaited(_syncPttMediaSession());
    unawaited(_syncSessionForeground());
    _session?.resumeRelay();
    _phoneCallPauseInProgress = false;
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
    if (state.isConnected ||
        state.isConnecting ||
        state.relayPausedForPhoneCall) {
      state = state.copyWith(
        lastError: AppStrings.cannotSwitchProfileConnected,
      );
      return true;
    }
    return false;
  }

  void selectProfile(int index) {
    if (index < 0 || index >= state.profiles.length) {
      return;
    }
    if (index == state.selectedServerIndex) {
      return;
    }
    _applySelectedProfile(index);
    unawaited(_reconnectToSelectedServerIfRequested());
  }

  void syncDraftFromSelectedProfile() {
    if (state.profiles.isEmpty) {
      return;
    }
    final index = state.selectedServerIndex.clamp(0, state.profiles.length - 1);
    _applySelectedProfile(index, persistSelection: false);
  }

  void _applySelectedProfile(int index, {bool persistSelection = true}) {
    final profile = state.profiles[index];
    state = state.copyWith(
      selectedServerIndex: index,
      draftProfile: profile,
      clearError: true,
      clearStatusMessage: true,
    );
    if (persistSelection) {
      unawaited(_store.setLastSelectedName(profile.name));
    }
  }

  Future<void> connectToSelectedProfile() async {
    syncDraftFromSelectedProfile();
    await toggleConnection();
  }

  void previousProfile() => moveSelectedServer(-1);

  void nextProfile() => moveSelectedServer(1);

  void moveSelectedServer(int offset) {
    if (!state.canNavigateProfiles || state.profiles.isEmpty) {
      return;
    }
    final target = (state.selectedServerIndex + offset)
        .clamp(0, state.profiles.length - 1);
    if (target == state.selectedServerIndex) {
      return;
    }
    _applySelectedProfile(target);
    UiSignalPlayer.playSwitch(_session);
    unawaited(_reconnectToSelectedServerIfRequested());
  }

  void moveProfileUp() => moveProfileInList(-1);

  void moveProfileDown() => moveProfileInList(1);

  Future<void> moveProfileInList(int offset) async {
    if (!state.canReorderProfiles || state.profiles.isEmpty) {
      return;
    }
    final from = state.selectedServerIndex.clamp(0, state.profiles.length - 1);
    final to = (from + offset).clamp(0, state.profiles.length - 1);
    if (from == to) {
      return;
    }
    final list = [...state.profiles];
    final item = list.removeAt(from);
    list.insert(to, item);
    await _persistProfiles(list);
    state = state.copyWith(
      profiles: list,
      selectedServerIndex: to,
      draftProfile: list[to],
      clearError: true,
      clearStatusMessage: true,
    );
    UiSignalPlayer.playSwitch(_session);
    unawaited(_reconnectToSelectedServerIfRequested());
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
      statusMessage: AppStrings.profileDeleted,
      clearError: true,
      clearStatusMessage: false,
    );
  }

  Future<void> _ensureSession() async {
    if (_ensureSessionFuture != null) {
      await _ensureSessionFuture;
      return;
    }
    _ensureSessionFuture = _ensureSessionImpl();
    try {
      await _ensureSessionFuture;
    } finally {
      _ensureSessionFuture = null;
    }
  }

  Future<void> _ensureSessionImpl() async {
    if (_sessionTeardownFuture != null) {
      await _sessionTeardownFuture;
    }
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
      _sessionSub = service.messages.listen(_onSessionMessage);
      await service.start();
      _session = service;
      service.setRxVolumePercent(state.rxVolumePercent);
      await UiSignalPlayer.ensureLoaded();
      UiSignalPlayer.loadSoundBank(service);
      if (NativePlatform.isMobile) {
        await _startMobileAudioStack();
      }
    } catch (e) {
      await _sessionSub?.cancel();
      _sessionSub = null;
      state = state.copyWith(
        sessionSupported: false,
        lastError: e.toString(),
        connectionChip: AppStrings.connectionStateUnsupported,
      );
    }
  }

  void _onSessionMessage(SessionWorkerMessage message) {
    if (_disposed) return;
    final prev = state;
    switch (message) {
      case SessionCoreInfoMessage(:final version, :final protocolVersion):
        state = state.copyWith(
          coreVersion: version,
          protocolVersion: protocolVersion,
          sessionSupported: true,
          clearError: true,
        );
        if (!NativePlatform.isMobile) {
          unawaited(
            AudioDeviceService.applyFromStore(
              ref.read(audioDeviceStoreProvider),
              microphoneStore: ref.read(microphoneSourceStoreProvider),
            ),
          );
        }
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
        if (connected && !state.isConnected) {
          telemetry.connected();
        } else if (!connected && !connecting && state.isConnected) {
          telemetry.connectionLost();
        }
        if (error != null && error.isNotEmpty) {
          telemetry.error(error);
        }
        if (connected && NativePlatform.isMobile) {
          unawaited(_applyVoiceAudioRoute());
        } else if (connecting &&
            !connected &&
            NativePlatform.isMobile) {
          // Prepare MODE_NORMAL before native opens AAudio playback on connect.
          unawaited(_applyVoiceAudioRoute());
        }
        if (!connected && !connecting) {
          _pttBurstGuard.reset();
          _pttDownInProgress = false;
          _cancelTxCountdown();
          if (state.isConnected &&
              !state.relayPausedForPhoneCall &&
              !_suppressTransientConnectionErrorTone &&
              !_skipNextManualDisconnectTone) {
            UiSignalPlayer.playManualDisconnect(_session);
          }
          _skipNextManualDisconnectTone = false;
        } else if (connecting &&
            !connected &&
            !reconnecting &&
            !state.isConnecting &&
            !_suppressTransientConnectionErrorTone) {
          UiSignalPlayer.playManualConnectStart(_session);
        } else if (connected && !state.isConnected) {
          UiSignalPlayer.playConnected(_session);
        }
        if (connected) {
          _suppressTransientConnectionErrorTone = false;
        }
        if (!_suppressTransientConnectionErrorTone) {
          if (error != null && error.isNotEmpty) {
            UiSignalPlayer.playConnectionError(_session);
          } else if (_userRequestedConnection &&
              state.isConnecting &&
              !connecting &&
              !connected &&
              !state.relayPausedForPhoneCall) {
            UiSignalPlayer.playConnectionError(_session);
          } else if (_userRequestedConnection &&
              state.isConnected &&
              !connected &&
              !connecting &&
              !state.relayPausedForPhoneCall) {
            UiSignalPlayer.playConnectionError(_session);
          }
        }
        state = state.copyWith(
          isConnected: connected,
          isConnecting: connecting,
          isReconnecting: reconnecting,
          txActive: connected ? state.txActive : false,
          isReceivingBroadcast: connected ? state.isReceivingBroadcast : false,
          callActive: connected ? state.callActive : false,
          udpReady: connected ? state.udpReady : false,
          protocolIncompatible:
              connected ? state.protocolIncompatible : false,
          connectionChip: connectionChipForTransport(
            connected: connected,
            connecting: connecting,
            reconnecting: reconnecting,
          ),
          signalChip: (!connected && !connecting)
              ? AppStrings.signalQualityDefault
              : state.signalChip,
          clearUplinkSignal: !connected && !connecting,
          lastError: error,
          clearError: error == null,
          statusInfo: reconnecting ? AppStrings.connectionStateReconnecting : state.statusInfo,
        );
        if (!connected &&
            !connecting &&
            NativePlatform.isMobile &&
            !state.relayPausedForPhoneCall) {
          unawaited(NativePlatform.releaseAudioSession());
          _session?.setAndroidBtVoiceRoute(false);
        }
        unawaited(_syncSessionForeground());
        unawaited(_syncPttMediaSession());
        syncWarmMicrophoneCapture();
      case SessionPttResultMessage(:final active, :final resultCode):
        _pttDownInProgress = false;
        state = state.copyWith(
          txActive: active,
          lastError: resultCode != 0 ? _pttError(resultCode) : state.lastError,
        );
        if (!active) {
          _cancelTxCountdown();
          syncWarmMicrophoneCapture();
        }
      case SessionNativeEventMessage(:final eventType, :final info):
        if (eventType == OwalkieEventType.pttLocked && state.txActive) {
          pttUp();
        }
        state = applyNativeSessionEvent(
          state,
          eventType: eventType,
          info: info,
        );
        _stopPttIfUiDisabled();
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
        if (eventType == OwalkieEventType.protocolError) {
          _suppressTransientConnectionErrorTone = false;
          UiSignalPlayer.playConnectionError(_session);
        }
      case SessionUnsupportedMessage():
        state = state.copyWith(
          sessionSupported: false,
          connectionChip: AppStrings.connectionStateUnsupported,
        );
      case SessionChannelActivityResultMessage():
        break;
      case SessionCallResultMessage(:final resultCode):
        state = state.copyWith(callActive: false);
        if (resultCode != 0) {
          state = state.copyWith(
            lastError: 'Call signal failed (error $resultCode).',
          );
        }
        syncWarmMicrophoneCapture();
      case SessionUplinkSignalMessage(:final percent):
        state = state.copyWith(
          uplinkSignalPercent: percent,
          signalChip: state.isReceivingBroadcast
              ? state.signalChip
              : AppStrings.signalQualityPercent(percent),
        );
      case SessionDisconnectCompleteMessage():
      case SessionShutdownCompleteMessage():
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
    if (pttUiEnabledFor(prev) && !pttUiEnabledFor(state) && state.txActive) {
      pttUp();
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

  void clearStatusMessage() {
    if (state.statusMessage == null) {
      return;
    }
    state = state.copyWith(clearStatusMessage: true);
  }

  void setRxVolume(int percent) {
    final value = percent.clamp(0, 200);
    state = state.copyWith(rxVolumePercent: value);
    _session?.setRxVolumePercent(value);
    _scheduleRxVolumePreview(value);
  }

  void finishRxVolumePreview(int percent) {
    _cancelRxVolumePreview();
    UiSignalPlayer.playVolumePreview(_session, percent.clamp(0, 200));
  }

  void _scheduleRxVolumePreview(int percent) {
    _cancelRxVolumePreview();
    _rxVolumePreviewTimer = Timer(_rxVolumePreviewDelay, () {
      UiSignalPlayer.playVolumePreview(_session, percent);
    });
  }

  void _cancelRxVolumePreview() {
    _rxVolumePreviewTimer?.cancel();
    _rxVolumePreviewTimer = null;
  }

  Future<void> _reconnectToSelectedServerIfRequested() async {
    if (!_userRequestedConnection &&
        !state.isConnected &&
        !state.isConnecting &&
        !state.relayPausedForPhoneCall) {
      return;
    }
    await _ensureSession();
    final session = _session;
    if (session == null) {
      return;
    }
    final profile = state.draftProfile;
    if (profile.host.trim().isEmpty) {
      return;
    }
    _suppressTransientConnectionErrorTone = true;
    session.switchServer(
      host: profile.host.trim(),
      port: profile.port,
      channel: profile.channel,
      repeater: profile.repeater,
    );
    state = state.copyWith(
      isConnected: false,
      isConnecting: true,
      isReconnecting: false,
      relayPausedForPhoneCall: false,
      protocolIncompatible: false,
      udpReady: false,
      txActive: false,
      isReceivingBroadcast: false,
      callActive: false,
      connectionChip: AppStrings.connectionStateConnecting,
      signalChip: AppStrings.signalQualityDefault,
      clearUplinkSignal: true,
      clearError: true,
    );
    unawaited(_syncSessionForeground());
    unawaited(_syncPttMediaSession());
  }

  void startScanning(ScanMode mode) {
    if (_scanLoopActive || state.profiles.isEmpty) {
      return;
    }
    UiSignalPlayer.playSwitch(_session);
    if (state.isConnected || state.isConnecting || state.relayPausedForPhoneCall) {
      // Scan is allowed while connected (skips current profile).
    }
    _scanMode = mode;
    _scanLoopActive = true;
    _scanCancellation = Completer<void>();
    telemetry.scanStart();
    state = state.copyWith(
      scanActive: true,
      statusMessage: AppStrings.scanStartedAnnouncement,
      clearError: true,
    );
    unawaited(_runScanLoop());
  }

  void stopScanning({bool announce = true}) {
    if (!_scanLoopActive && !state.scanActive) {
      return;
    }
    if (announce) {
      UiSignalPlayer.playSwitch(_session);
    }
    _scanLoopActive = false;
    _scanMode = null;
    final completer = _scanCancellation;
    _scanCancellation = null;
    if (completer != null && !completer.isCompleted) {
      completer.complete();
    }
    telemetry.scanStop();
    state = state.copyWith(
      scanActive: false,
      statusMessage: announce ? AppStrings.scanStoppedAnnouncement : state.statusMessage,
    );
    unawaited(_teardownIdleSession());
  }

  bool _hasCurrentConnectionActivity() =>
      state.txActive || state.isReceivingBroadcast;

  Future<void> _playScanActivityFoundVibration() async {
    await Haptics.scanActivityFound();
  }

  Future<void> _cancellableScanDelay(Duration duration) async {
    final completer = _scanCancellation;
    if (completer == null || completer.isCompleted) return;
    await Future.any<void>([
      Future<void>.delayed(duration),
      completer.future,
    ]);
  }

  Future<void> _runScanLoop() async {
    await _ensureSession();
    while (_scanLoopActive && _scanCancellation != null && !_scanCancellation!.isCompleted) {
      if (_disposed) break;
      if (state.profiles.isEmpty) {
        await _cancellableScanDelay(_scanInterval);
        continue;
      }
      final snapshot = List<ServerProfile>.from(state.profiles);
      final skipIndex =
          state.isConnected ? state.selectedServerIndex : -1;
      ServerProfile? foundProfile;
      var foundIndex = -1;
      for (var index = 0; index < snapshot.length; index++) {
        if (!_scanLoopActive || _scanCancellation?.isCompleted == true) break;
        if (index == skipIndex) {
          continue;
        }
        final profile = snapshot[index];
        final endpoint = resolveProfileEndpoint(profile.host, profile.port);
        if (endpoint == null) {
          continue;
        }
        final result = await _session?.checkChannelActivity(
          host: endpoint.host,
          port: endpoint.port,
          channel: profile.channel,
          timeoutMs: _scanQueryTimeoutMs,
        );
        if (result != null && result.active) {
          foundProfile = profile;
          foundIndex = state.profiles.indexWhere(
            (p) =>
                p.name == profile.name &&
                p.host == profile.host &&
                p.channel == profile.channel,
          );
          if (foundIndex < 0) {
            foundIndex = index;
          }
          break;
        }
      }
      if (!_scanLoopActive || _scanCancellation?.isCompleted == true) {
        break;
      }
      if (foundProfile != null) {
        final profile = foundProfile;
        telemetry.scanFound(profile.name);
        if (_hasCurrentConnectionActivity()) {
          state = state.copyWith(
            statusMessage: AppStrings.scanFoundActivityToast(profile.name),
          );
          await _playScanActivityFoundVibration();
          await _cancellableScanDelay(_scanInterval);
          continue;
        }
        final idx = foundIndex.clamp(0, state.profiles.length - 1);
        if (state.profiles.isNotEmpty && idx != state.selectedServerIndex) {
          _applySelectedProfile(idx);
        }
        state = state.copyWith(
          draftProfile: profile,
          statusMessage: AppStrings.scanFoundServerAnnouncement(profile.name),
        );
        await _connectToProfileFromScan(profile);
        if (_scanMode == ScanMode.oneShot) {
          stopScanning(announce: false);
          break;
        }
        await _cancellableScanDelay(_scanInterval);
        continue;
      }
      await _cancellableScanDelay(_scanInterval);
    }
  }

  Future<void> _connectToProfileFromScan(ServerProfile profile) async {
    await _ensureSession();
    final session = _session;
    if (session == null) {
      return;
    }
    if (state.isConnected || state.isConnecting) {
      await session.disconnect();
    }
    _userRequestedConnection = true;
    if (NativePlatform.isMobile) {
      await _applyVoiceAudioRoute();
    }
    session.connect(
      host: profile.host.trim(),
      port: profile.port,
      channel: profile.channel,
      repeater: profile.repeater,
    );
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

  Future<void> _syncPttMediaSession() async {
    if (!NativePlatform.isAndroid) {
      return;
    }
    final active = state.isConnected &&
        !state.relayPausedForPhoneCall &&
        _mediaButtonPttStore.isEnabled();
    await NativePlatform.syncPttMediaSession(active: active);
  }

  Future<void> externalConnect() async {
    if (state.isConnected || state.isConnecting || state.relayPausedForPhoneCall) {
      return;
    }
    await toggleConnection();
  }

  Future<void> externalDisconnect() async {
    if (!state.isConnected && !state.isConnecting && !state.relayPausedForPhoneCall) {
      return;
    }
    await toggleConnection();
  }

  Future<void> externalSwitchProfile(int step) async {
    if (state.profiles.length <= 1 || step == 0) {
      return;
    }
    moveSelectedServer(step);
  }

  void togglePttLatch() {
    if (!pttUiEnabledFor(state)) {
      return;
    }
    if (state.txActive) {
      pttUp();
    } else {
      pttDown();
    }
  }

  /// Kotlin [MainActivity.sendActivityFocusState] + warm mic sync (mobile only).
  void onAppLifecycleChanged(AppLifecycleState lifecycle) {
    if (!NativePlatform.isMobile) {
      return;
    }
    final focused = lifecycle == AppLifecycleState.resumed;
    if (_appInForeground == focused) {
      return;
    }
    _appInForeground = focused;
    if (!focused && state.txActive) {
      pttUp();
    }
    syncWarmMicrophoneCapture();
  }

  void syncWarmMicrophoneCapture() {
    if (!NativePlatform.isMobile || _session == null) {
      return;
    }
    _session!.syncWarmCapture(
      warmMicEnabled: _warmMicRecorderStore.isEnabled(),
      appInForeground: _appInForeground,
    );
  }

  void syncAndroidBtVoiceRoute(bool enabled) {
    _session?.setAndroidBtVoiceRoute(enabled);
  }

  Future<void> applyAudioOutputProfile(AudioOutputProfile profile) async {
    if (!NativePlatform.isMobile) {
      return;
    }
    await AudioOutputProfileService.persistAndApply(
      store: ref.read(audioOutputProfileStoreProvider),
      profile: profile,
    );
  }

  Future<void> _syncSessionForeground() async {
    if (!NativePlatform.isAndroid) {
      return;
    }
    final sessionDesired = sessionKeepAlive;
    if (!sessionDesired) {
      if (_sessionForegroundActive) {
        _sessionForegroundActive = false;
        await NativePlatform.stopSessionForeground();
      }
      return;
    }
    await NativePlatform.ensureNotificationPermission();
    if (!_sessionForegroundActive) {
      _sessionForegroundActive = true;
      await NativePlatform.startSessionForeground(connected: state.isConnected);
      return;
    }
    await NativePlatform.updateSessionForeground(connected: state.isConnected);
  }

  Future<void> toggleConnection() async {
    await _ensureSession();
    final session = _session;
    if (session == null || !state.sessionSupported) {
      return;
    }
    if (state.isConnected || state.isConnecting || state.relayPausedForPhoneCall) {
      UiSignalPlayer.playSwitch(session);
      _userRequestedConnection = false;
      _suppressTransientConnectionErrorTone = false;
      _skipNextManualDisconnectTone = true;
      UiSignalPlayer.playManualDisconnect(session);
      state = state.copyWith(relayPausedForPhoneCall: false);
      telemetry.disconnect(reason: 'user');

      // Await worker confirmation before tearing down
      await session.disconnect();

      // If user reconnected during disconnect, keep the session alive
      if (!sessionKeepAlive) {
        unawaited(_syncSessionForeground());
        await _releaseMobileAudioAndTeardown();
      }
      return;
    }
    if (NativePlatform.isAndroid) {
      await NativePlatform.ensureNotificationPermission();
    }
    final p = state.profile;
    if (p.host.trim().isEmpty) {
      state = state.copyWith(
        lastError:
            'Enter server host (LAN IP of the relay, not 127.0.0.1 on a phone).',
      );
      return;
    }
    _userRequestedConnection = true;
    if (NativePlatform.isMobile) {
      await _applyVoiceAudioRoute();
    }
    UiSignalPlayer.playSwitch(session);
    telemetry.connect(host: p.host.trim(), port: p.port, channel: p.channel);
    session.connect(
      host: p.host.trim(),
      port: p.port,
      channel: p.channel,
      repeater: p.repeater,
    );
    state = state.copyWith(
      statusMessage: AppStrings.connectingToSelectedServer,
      clearError: true,
      clearStatusMessage: false,
    );
    unawaited(_syncSessionForeground());
  }

  String _pttError(int code) {
    return switch (code) {
      6 => 'Microphone capture failed. Check permission and try again.',
      9 => 'Not connected yet — wait for Connected status.',
      _ => 'Push-to-talk failed (error $code).',
    };
  }

  void pttDown() {
    if (_pttDownInProgress) {
      return;
    }
    if (!pttUiEnabledFor(state)) {
      return;
    }
    if (state.txActive) {
      return;
    }
    if (!_pttBurstGuard.onPressAttempt()) {
      state = state.copyWith(pttBurstPressBlocked: _pttBurstGuard.pressBlocked);
      return;
    }
    _pttDownInProgress = true;
    state = state.copyWith(isReceivingBroadcast: false);
    telemetry.pttDown();
    if (NativePlatform.isMobile) {
      unawaited(_pttDownAsync());
      return;
    }
    _session?.pttDown();
  }

  Future<void> _pttDownAsync() async {
    final micOk = await NativePlatform.ensureMicrophonePermission();
    if (!micOk) {
      _pttDownInProgress = false;
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
    final roger = _rogerStore.getSelectedPattern();
    _session?.pttUp(rogerPoints: encodeSignalPoints(roger.points));
    _pttBurstGuard.onRelease();
    telemetry.pttUp(success: true);
  }

  void sendCall() {
    if (!pttUiEnabledFor(state) ||
        state.txActive ||
        state.callActive) {
      return;
    }
    final pattern = _callingStore.getSelectedPattern();
    if (pattern.points.isEmpty) {
      return;
    }
    state = state.copyWith(callActive: true, clearError: true);
    _session?.sendCall(
      points: encodeSignalPoints(pattern.points),
      repeatCount: pattern.repeatCount ?? 1,
    );
  }

  void previewSignalPattern(List<SignalPoint> points) {
    SignalPreviewPlayer.playPattern(_session, points);
  }

  void _dispose() {
    if (_disposed) return;
    _disposed = true;
    _pttDownInProgress = false;
    _cancelTxCountdown();
    _cancelRxVolumePreview();
    _pttBurstGuard.onBlockedChanged = null;
    _pttBurstGuard.reset();
    unawaited(Haptics.parallelTxCollision(active: false));
    unawaited(ScreenWake.setTransmitting(false));
    _platformSub?.cancel();
    _platformSub = null;
    _windowsPttSub?.cancel();
    _windowsPttSub = null;
    _deepLinkSub?.cancel();
    _deepLinkSub = null;
    stopScanning(announce: false);
    unawaited(_audioInterruption?.stop());
    _audioInterruption = null;
    if (_sessionForegroundActive) {
      _sessionForegroundActive = false;
      unawaited(NativePlatform.stopSessionForeground());
    }
    unawaited(NativePlatform.syncPttMediaSession(active: false));
    _sessionSub?.cancel();
    _session?.dispose();
    _session = null;
  }
}
