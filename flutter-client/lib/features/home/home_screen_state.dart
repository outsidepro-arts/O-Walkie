import '../../domain/server_profile.dart';
import '../../l10n/app_strings.dart';

class HomeScreenState {
  const HomeScreenState({
    this.connectionDetailsExpanded = true,
    this.selectedServerIndex = 0,
    this.profile = const ServerProfile(),
    this.rxVolumePercent = 100,
    this.scanActive = false,
    this.isConnecting = false,
    this.isConnected = false,
    this.txActive = false,
    this.connectionChip = AppStrings.connectionStateDisconnected,
    this.signalChip = AppStrings.signalQualityDefault,
    this.coreVersion = '',
    this.protocolVersion = 0,
    this.sessionSupported = true,
    this.lastError,
  });

  final bool connectionDetailsExpanded;
  final int selectedServerIndex;
  final ServerProfile profile;
  final int rxVolumePercent;
  final bool scanActive;
  final bool isConnecting;
  final bool isConnected;
  final bool txActive;
  final String connectionChip;
  final String signalChip;
  final String coreVersion;
  final int protocolVersion;
  final bool sessionSupported;
  final String? lastError;

  HomeScreenState copyWith({
    bool? connectionDetailsExpanded,
    int? selectedServerIndex,
    ServerProfile? profile,
    int? rxVolumePercent,
    bool? scanActive,
    bool? isConnecting,
    bool? isConnected,
    bool? txActive,
    String? connectionChip,
    String? signalChip,
    String? coreVersion,
    int? protocolVersion,
    bool? sessionSupported,
    String? lastError,
    bool clearError = false,
  }) {
    return HomeScreenState(
      connectionDetailsExpanded:
          connectionDetailsExpanded ?? this.connectionDetailsExpanded,
      selectedServerIndex: selectedServerIndex ?? this.selectedServerIndex,
      profile: profile ?? this.profile,
      rxVolumePercent: rxVolumePercent ?? this.rxVolumePercent,
      scanActive: scanActive ?? this.scanActive,
      isConnecting: isConnecting ?? this.isConnecting,
      isConnected: isConnected ?? this.isConnected,
      txActive: txActive ?? this.txActive,
      connectionChip: connectionChip ?? this.connectionChip,
      signalChip: signalChip ?? this.signalChip,
      coreVersion: coreVersion ?? this.coreVersion,
      protocolVersion: protocolVersion ?? this.protocolVersion,
      sessionSupported: sessionSupported ?? this.sessionSupported,
      lastError: clearError ? null : (lastError ?? this.lastError),
    );
  }
}
