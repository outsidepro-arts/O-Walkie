import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_core/owalkie_core.dart';
import 'package:owalkie_app/features/home/home_screen_state.dart';
import 'package:owalkie_app/features/home/session_event_mapper.dart';
import 'package:owalkie_app/domain/server_profile.dart';
import 'package:owalkie_app/l10n/app_strings.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  AppStrings.bind(lookupAppLocalizations(const Locale('en')));
  group('connectionDisplayChipFor', () {
    test('maps transmitting over connected', () {
      final state = HomeScreenState(isConnected: true, udpReady: true, txActive: true);
      expect(
        connectionDisplayChipFor(state),
        AppStrings.connectionStateTransmitting,
      );
    });

    test('maps receiving over connected', () {
      final state = HomeScreenState(
        isConnected: true,
        udpReady: true,
        isReceivingBroadcast: true,
      );
      expect(
        connectionDisplayChipFor(state),
        AppStrings.connectionStateReceiving,
      );
    });

    test('maps scanning when idle', () {
      final state = HomeScreenState(scanActive: true);
      expect(
        connectionDisplayChipFor(state),
        AppStrings.connectionStateScanning,
      );
    });

    test('maps partial when connected without udp', () {
      final state = HomeScreenState(isConnected: true, udpReady: false);
      expect(
        connectionDisplayChipFor(state),
        AppStrings.connectionStatePartial,
      );
    });

    test('maps protocol incompatible', () {
      final state = HomeScreenState(protocolIncompatible: true);
      expect(
        connectionDisplayChipFor(state),
        AppStrings.connectionStateProtocolIncompatible,
      );
    });
  });

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

  group('profile navigation', () {
    test('canNavigateProfiles when collapsed with servers while connected', () {
      final state = HomeScreenState(
        connectionDetailsExpanded: false,
        profiles: const [
          ServerProfile(name: 'a', host: '1', port: 1, channel: 'c'),
          ServerProfile(name: 'b', host: '2', port: 2, channel: 'c'),
        ],
        isConnected: true,
      );
      expect(state.canNavigateProfiles, isTrue);
      expect(state.canSelectProfiles, isFalse);
      expect(state.hasPreviousProfile, isFalse);
      expect(state.hasNextProfile, isTrue);
    });

    test('hasPreviousProfile at last index', () {
      final state = HomeScreenState(
        connectionDetailsExpanded: false,
        profiles: const [
          ServerProfile(name: 'a', host: '1', port: 1, channel: 'c'),
          ServerProfile(name: 'b', host: '2', port: 2, channel: 'c'),
        ],
        selectedServerIndex: 1,
        isConnected: true,
      );
      expect(state.hasPreviousProfile, isTrue);
      expect(state.hasNextProfile, isFalse);
    });

    test('canNavigateProfiles false when details expanded', () {
      final state = HomeScreenState(
        connectionDetailsExpanded: true,
        profiles: const [
          ServerProfile(name: 'a', host: '1', port: 1, channel: 'c'),
          ServerProfile(name: 'b', host: '2', port: 2, channel: 'c'),
        ],
      );
      expect(state.canNavigateProfiles, isFalse);
    });
  });

  group('applyNativeSessionEvent', () {
    late HomeScreenState base;

    setUp(() {
      base = HomeScreenState(isConnected: true, txActive: true);
    });

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
        HomeScreenState(),
        eventType: OwalkieEventType.rxBroadcastStart,
        info: 'true',
      );
      expect(next.signalChip, AppStrings.signalRxBusy);
      expect(next.isReceivingBroadcast, isTrue);
    });

    test('rx broadcast end restores uplink percent', () {
      final receiving = HomeScreenState(
        isReceivingBroadcast: true,
        uplinkSignalPercent: 72,
        signalChip: AppStrings.signalRxActive,
      );
      final next = applyNativeSessionEvent(
        receiving,
        eventType: OwalkieEventType.rxBroadcastEnd,
        info: '',
      );
      expect(next.isReceivingBroadcast, isFalse);
      expect(next.signalChip, AppStrings.signalQualityPercent(72));
    });

    test('parallel tx when transmitting and receiving', () {
      final receiving = HomeScreenState(isReceivingBroadcast: true);
      expect(receiving.parallelTxActive, isFalse);
      final parallel = receiving.copyWith(txActive: true);
      expect(parallel.parallelTxActive, isTrue);
      expect(
        connectionDisplayChipFor(parallel),
        AppStrings.connectionStateParallelTx,
      );
    });
  });

  group('pttButtonLabel', () {
    test('shows stop label when transmitting', () {
      expect(
        pttButtonLabel(
          pttUiEnabled: true,
          txActive: true,
          pttServerLocked: false,
          pttLockSec: 0,
          txCountdownSec: 0,
        ),
        AppStrings.pttStopTalking,
      );
    });

    test('shows lock countdown when server locked', () {
      expect(
        pttButtonLabel(
          pttUiEnabled: false,
          txActive: false,
          pttServerLocked: true,
          pttLockSec: 4,
          txCountdownSec: 0,
        ),
        AppStrings.pttLockedCountdown(4),
      );
    });

    test('shows unavailable when disabled without countdown', () {
      expect(
        pttButtonLabel(
          pttUiEnabled: false,
          txActive: false,
          pttServerLocked: false,
          pttLockSec: 0,
          txCountdownSec: 0,
        ),
        AppStrings.pttUnavailable,
      );
    });

    test('pttEnabled respects server lock and burst guard', () {
      expect(
        pttEnabled(
          sessionSupported: true,
          isConnected: true,
          pttServerLocked: true,
          pttBurstPressBlocked: false,
        ),
        isFalse,
      );
      expect(
        pttEnabled(
          sessionSupported: true,
          isConnected: true,
          pttServerLocked: false,
          pttBurstPressBlocked: true,
        ),
        isFalse,
      );
    });
  });
}
