import 'package:owalkie_core/owalkie_core.dart';

import '../../l10n/app_strings.dart';
import 'home_screen_state.dart';

/// Pure mapping from native session events to [HomeScreenState] (testable).
HomeScreenState applyNativeSessionEvent(
  HomeScreenState state, {
  required int eventType,
  required String info,
}) {
  switch (eventType) {
    case OwalkieEventType.connected:
      return state.copyWith(
        clearError: true,
        clearStatusInfo: true,
        pttServerLocked: false,
        pttLockSec: 0,
        txCountdownSec: 0,
      );
    case OwalkieEventType.disconnected:
    case OwalkieEventType.connectionFailed:
      return state.copyWith(
        pttServerLocked: false,
        pttLockSec: 0,
        txCountdownSec: 0,
        txActive: false,
        lastError: info.isNotEmpty ? info : state.lastError,
      );
    case OwalkieEventType.protocolError:
      return state.copyWith(
        pttServerLocked: false,
        pttLockSec: 0,
        txCountdownSec: 0,
        txActive: false,
        lastError: info.isNotEmpty ? info : 'Protocol error',
      );
    case OwalkieEventType.connectionLost:
      return state.copyWith(
        txActive: false,
        statusInfo: AppStrings.connectionStateReconnecting,
      );
    case OwalkieEventType.rxBroadcastStart:
      final busy = info == 'true';
      return state.copyWith(
        signalChip: busy ? AppStrings.signalRxBusy : AppStrings.signalRxActive,
      );
    case OwalkieEventType.rxBroadcastEnd:
      return state.copyWith(signalChip: AppStrings.signalQualityDefault);
    case OwalkieEventType.pttLocked:
      final sec = int.tryParse(info) ?? 0;
      return state.copyWith(
        pttServerLocked: true,
        pttLockSec: sec,
        txActive: false,
      );
    case OwalkieEventType.pttUnlocked:
      return state.copyWith(
        pttServerLocked: false,
        pttLockSec: 0,
      );
    case OwalkieEventType.txCountdownStart:
      final sec = int.tryParse(info) ?? 0;
      return state.copyWith(txCountdownSec: sec);
    case OwalkieEventType.txStop:
      return state.copyWith(
        txCountdownSec: 0,
        statusInfo: info.isNotEmpty ? info : null,
      );
    default:
      return state;
  }
}

String connectionChipForTransport({
  required bool connected,
  required bool connecting,
  required bool reconnecting,
}) {
  if (connected) {
    return AppStrings.connectionStateConnected;
  }
  if (connecting && reconnecting) {
    return AppStrings.connectionStateReconnecting;
  }
  if (connecting) {
    return AppStrings.connectionStateConnecting;
  }
  return AppStrings.connectionStateDisconnected;
}

String pttButtonLabel({
  required bool txActive,
  required bool pttServerLocked,
  required int pttLockSec,
  required int txCountdownSec,
}) {
  if (txActive) {
    return AppStrings.pttActive;
  }
  if (pttServerLocked && pttLockSec > 0) {
    return AppStrings.pttLockedCountdown(pttLockSec);
  }
  if (txCountdownSec > 0) {
    return AppStrings.pttTxCountdown(txCountdownSec);
  }
  return AppStrings.pttHold;
}

bool pttEnabled({
  required bool sessionSupported,
  required bool isConnected,
  required bool pttServerLocked,
}) {
  return sessionSupported && isConnected && !pttServerLocked;
}
