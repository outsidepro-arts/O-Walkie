import '../../domain/server_profile.dart';
import '../../l10n/app_strings.dart';

class HomeScreenState {
  HomeScreenState({
    this.connectionDetailsExpanded = false,
    this.profiles = const [ServerProfile()],
    this.selectedServerIndex = 0,
    this.draftProfile = const ServerProfile(),
    this.rxVolumePercent = 100,
    this.scanActive = false,
    this.isConnecting = false,
    this.isConnected = false,
    this.isReconnecting = false,
    this.relayPausedForPhoneCall = false,
    this.txActive = false,
    this.pttServerLocked = false,
    this.pttBurstPressBlocked = false,
    this.pttLockSec = 0,
    this.txCountdownSec = 0,
    this.isReceivingBroadcast = false,
    this.callActive = false,
    this.protocolIncompatible = false,
    this.udpReady = false,
    this.uplinkSignalPercent,
    String? connectionChip,
    String? signalChip,
    this.coreVersion = '',
    this.protocolVersion = 0,
    this.sessionSupported = true,
    this.lastError,
    this.statusInfo,
    this.statusMessage,
  })  : connectionChip =
            connectionChip ?? AppStrings.connectionStateDisconnected,
        signalChip = signalChip ?? AppStrings.signalQualityDefault;

  final bool connectionDetailsExpanded;
  final List<ServerProfile> profiles;
  final int selectedServerIndex;
  /// Unsaved connection form values (Kotlin: input fields until Save).
  final ServerProfile draftProfile;
  final int rxVolumePercent;
  final bool scanActive;
  final bool isConnecting;
  final bool isConnected;
  final bool isReconnecting;
  final bool relayPausedForPhoneCall;
  final bool txActive;
  final bool pttServerLocked;
  final bool pttBurstPressBlocked;
  final int pttLockSec;
  final int txCountdownSec;
  final bool isReceivingBroadcast;
  final bool callActive;
  final bool protocolIncompatible;
  final bool udpReady;
  final int? uplinkSignalPercent;
  final String connectionChip;
  final String signalChip;
  final String coreVersion;
  final int protocolVersion;
  final bool sessionSupported;
  final String? lastError;
  final String? statusInfo;
  final String? statusMessage;

  /// Active form profile (draft, not necessarily persisted yet).
  ServerProfile get profile => draftProfile;

  bool get canSelectProfiles => profiles.isNotEmpty;

  /// Prev/next server buttons (Kotlin [updateServerNavigationButtons]).
  bool get canNavigateProfiles =>
      !connectionDetailsExpanded && profiles.isNotEmpty;

  bool get hasPreviousProfile =>
      canNavigateProfiles && selectedServerIndex > 0;

  bool get hasNextProfile =>
      canNavigateProfiles && selectedServerIndex < profiles.length - 1;

  /// Move up/down in list (Kotlin [updateServerNavigationButtons] reorder row).
  bool get canReorderProfiles =>
      connectionDetailsExpanded && profiles.isNotEmpty;

  bool get canMoveProfileUp =>
      canReorderProfiles && selectedServerIndex > 0;

  bool get canMoveProfileDown =>
      canReorderProfiles && selectedServerIndex < profiles.length - 1;

  bool get canSwitchProfiles => canSelectProfiles && profiles.length > 1;

  bool get parallelTxActive => txActive && isReceivingBroadcast;

  /// Left status chip — same priority as Kotlin [MainActivity.updateStatusChips].
  String get connectionDisplayChip => connectionDisplayChipFor(this);

  HomeScreenState copyWith({
    bool? connectionDetailsExpanded,
    List<ServerProfile>? profiles,
    int? selectedServerIndex,
    ServerProfile? draftProfile,
    int? rxVolumePercent,
    bool? scanActive,
    bool? isConnecting,
    bool? isConnected,
    bool? isReconnecting,
    bool? relayPausedForPhoneCall,
    bool? txActive,
    bool? pttServerLocked,
    bool? pttBurstPressBlocked,
    int? pttLockSec,
    int? txCountdownSec,
    bool? isReceivingBroadcast,
    bool? callActive,
    bool? protocolIncompatible,
    bool? udpReady,
    int? uplinkSignalPercent,
    String? connectionChip,
    String? signalChip,
    String? coreVersion,
    int? protocolVersion,
    bool? sessionSupported,
    String? lastError,
    String? statusInfo,
    String? statusMessage,
    bool clearError = false,
    bool clearStatusInfo = false,
    bool clearStatusMessage = false,
    bool clearUplinkSignal = false,
  }) {
    return HomeScreenState(
      connectionDetailsExpanded:
          connectionDetailsExpanded ?? this.connectionDetailsExpanded,
      profiles: profiles ?? this.profiles,
      selectedServerIndex: selectedServerIndex ?? this.selectedServerIndex,
      draftProfile: draftProfile ?? this.draftProfile,
      rxVolumePercent: rxVolumePercent ?? this.rxVolumePercent,
      scanActive: scanActive ?? this.scanActive,
      isConnecting: isConnecting ?? this.isConnecting,
      isConnected: isConnected ?? this.isConnected,
      isReconnecting: isReconnecting ?? this.isReconnecting,
      relayPausedForPhoneCall:
          relayPausedForPhoneCall ?? this.relayPausedForPhoneCall,
      txActive: txActive ?? this.txActive,
      pttServerLocked: pttServerLocked ?? this.pttServerLocked,
      pttBurstPressBlocked:
          pttBurstPressBlocked ?? this.pttBurstPressBlocked,
      pttLockSec: pttLockSec ?? this.pttLockSec,
      txCountdownSec: txCountdownSec ?? this.txCountdownSec,
      isReceivingBroadcast:
          isReceivingBroadcast ?? this.isReceivingBroadcast,
      callActive: callActive ?? this.callActive,
      protocolIncompatible: protocolIncompatible ?? this.protocolIncompatible,
      udpReady: udpReady ?? this.udpReady,
      uplinkSignalPercent: clearUplinkSignal
          ? null
          : (uplinkSignalPercent ?? this.uplinkSignalPercent),
      connectionChip: connectionChip ?? this.connectionChip,
      signalChip: signalChip ?? this.signalChip,
      coreVersion: coreVersion ?? this.coreVersion,
      protocolVersion: protocolVersion ?? this.protocolVersion,
      sessionSupported: sessionSupported ?? this.sessionSupported,
      lastError: clearError ? null : (lastError ?? this.lastError),
      statusInfo: clearStatusInfo ? null : (statusInfo ?? this.statusInfo),
      statusMessage:
          clearStatusMessage ? null : (statusMessage ?? this.statusMessage),
    );
  }
}

/// Left connection chip text; mirrors Kotlin [MainActivity.updateStatusChips] priority.
String connectionDisplayChipFor(HomeScreenState state) {
  if (!state.sessionSupported) {
    return AppStrings.connectionStateUnsupported;
  }
  if (state.protocolIncompatible) {
    return AppStrings.connectionStateProtocolIncompatible;
  }
  if (state.relayPausedForPhoneCall) {
    return AppStrings.connectionStatePausedPhoneCall;
  }
  if (state.callActive) {
    return AppStrings.connectionStateCalling;
  }
  if (state.parallelTxActive) {
    return AppStrings.connectionStateParallelTx;
  }
  if (state.txActive) {
    return AppStrings.connectionStateTransmitting;
  }
  if (state.isConnected && state.isReceivingBroadcast) {
    return AppStrings.connectionStateReceiving;
  }
  if (state.scanActive) {
    return AppStrings.connectionStateScanning;
  }
  if (state.isConnecting) {
    if (state.isReconnecting) {
      return AppStrings.connectionStateReconnecting;
    }
    return AppStrings.connectionStateConnecting;
  }
  if (state.isConnected && state.udpReady) {
    return AppStrings.connectionStateConnected;
  }
  if (state.isConnected) {
    return AppStrings.connectionStatePartial;
  }
  return AppStrings.connectionStateDisconnected;
}
