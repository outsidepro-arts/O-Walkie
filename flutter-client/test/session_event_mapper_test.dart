import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_core/owalkie_core.dart';
import 'package:owalkie_app/features/home/home_screen_state.dart';
import 'package:owalkie_app/features/home/session_event_mapper.dart';
import 'package:owalkie_app/l10n/app_strings.dart';

void main() {
  group('connectionChipForTransport', () {
    test('maps connected', () {
      expect(
        connectionChipForTransport(connected: true, connecting: false, reconnecting: false),
        AppStrings.connectionStateConnected,
      );
    });

    test('maps reconnecting', () {
      expect(
        connectionChipForTransport(connected: false, connecting: true, reconnecting: true),
        AppStrings.connectionStateReconnecting,
      );
    });

    test('maps initial connecting', () {
      expect(
        connectionChipForTransport(connected: false, connecting: true, reconnecting: false),
        AppStrings.connectionStateConnecting,
      );
    });
  });

  group('applyNativeSessionEvent', () {
    const base = HomeScreenState(isConnected: true, txActive: true);

    test('ptt lock parses countdown and blocks tx', () {
      final next = applyNativeSessionEvent(
        base,
        eventType: OwalkieEventType.pttLocked,
        info: '5',
      );
      expect(next.pttServerLocked, isTrue);
      expect(next.pttLockSec, 5);
      expect(next.txActive, isFalse);
    });

    test('ptt unlock clears lock', () {
      final locked = base.copyWith(pttServerLocked: true, pttLockSec: 3);
      final next = applyNativeSessionEvent(
        locked,
        eventType: OwalkieEventType.pttUnlocked,
        info: '',
      );
      expect(next.pttServerLocked, isFalse);
      expect(next.pttLockSec, 0);
    });

    test('protocol error surfaces message', () {
      final next = applyNativeSessionEvent(
        base,
        eventType: OwalkieEventType.protocolError,
        info: 'TLS not supported',
      );
      expect(next.lastError, 'TLS not supported');
      expect(next.txActive, isFalse);
    });

    test('rx broadcast start busy mode', () {
      final next = applyNativeSessionEvent(
        const HomeScreenState(),
        eventType: OwalkieEventType.rxBroadcastStart,
        info: 'true',
      );
      expect(next.signalChip, AppStrings.signalRxBusy);
      expect(next.isReceivingBroadcast, isTrue);
    });

    test('parallel tx when transmitting and receiving', () {
      const receiving = HomeScreenState(isReceivingBroadcast: true);
      expect(receiving.parallelTxActive, isFalse);
      final parallel = receiving.copyWith(txActive: true);
      expect(parallel.parallelTxActive, isTrue);
      expect(
        parallel.connectionDisplayChip,
        AppStrings.connectionStateParallelTx,
      );
    });
  });

  group('pttButtonLabel', () {
    test('shows lock countdown', () {
      expect(
        pttButtonLabel(
          txActive: false,
          pttServerLocked: true,
          pttLockSec: 4,
          txCountdownSec: 0,
        ),
        AppStrings.pttLockedCountdown(4),
      );
    });

    test('pttEnabled respects server lock', () {
      expect(
        pttEnabled(
          sessionSupported: true,
          isConnected: true,
          pttServerLocked: true,
        ),
        isFalse,
      );
    });
  });
}
