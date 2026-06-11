import '../../domain/server_profile.dart';
import '../../l10n/app_strings.dart';

class HomeScreenState {
  const HomeScreenState({
    this.connectionDetailsExpanded = true,
    this.profiles = const [ServerProfile()],
    this.selectedServerIndex = 0,
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

  ServerProfile get profile {
    if (profiles.isEmpty) {
      return const ServerProfile();
    }
    final index = selectedServerIndex.clamp(0, profiles.length - 1);
    return profiles[index];
  }

  bool get canSwitchProfiles =>
      !isConnected && !isConnecting && profiles.length > 1;

  HomeScreenState copyWith({
    bool? connectionDetailsExpanded,
    List<ServerProfile>? profiles,
    int? selectedServerIndex,
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

  HomeScreenState withProfileAt(int index, ServerProfile profile) {
    final list = [...profiles];
    if (index < 0 || index >= list.length) {
      return this;
    }
    list[index] = profile;
    return copyWith(profiles: list);
  }
}
