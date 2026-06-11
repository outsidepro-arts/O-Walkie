import '../../domain/server_profile.dart';
import '../../l10n/app_strings.dart';

class HomeScreenState {
  const HomeScreenState({
    this.connectionDetailsExpanded = true,
    this.profiles = const [ServerProfile()],
    this.selectedServerIndex = 0,
    this.draftProfile = const ServerProfile(),
    this.rxVolumePercent = 100,
    this.scanActive = false,
    this.isConnecting = false,
    this.isConnected = false,
    this.isReconnecting = false,
    this.txActive = false,
    this.pttServerLocked = false,
    this.pttLockSec = 0,
    this.txCountdownSec = 0,
    this.connectionChip = AppStrings.connectionStateDisconnected,
    this.signalChip = AppStrings.signalQualityDefault,
    this.coreVersion = '',
    this.protocolVersion = 0,
    this.sessionSupported = true,
    this.lastError,
    this.statusInfo,
    this.statusMessage,
  });

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
  final bool txActive;
  final bool pttServerLocked;
  final int pttLockSec;
  final int txCountdownSec;
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

  bool get canSelectProfiles =>
      !isConnected && !isConnecting && profiles.isNotEmpty;

  bool get canSwitchProfiles => canSelectProfiles && profiles.length > 1;

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
    bool? txActive,
    bool? pttServerLocked,
    int? pttLockSec,
    int? txCountdownSec,
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
      txActive: txActive ?? this.txActive,
      pttServerLocked: pttServerLocked ?? this.pttServerLocked,
      pttLockSec: pttLockSec ?? this.pttLockSec,
      txCountdownSec: txCountdownSec ?? this.txCountdownSec,
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
