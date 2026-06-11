import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/a11y/ptt_a11y.dart';
import 'package:owalkie_app/l10n/a11y_strings.dart';
import 'package:owalkie_app/l10n/app_strings.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  AppStrings.bind(lookupAppLocalizations(const Locale('en')));

  group('resolvePttA11yBucket', () {
    test('countdown when server lock with seconds', () {
      expect(
        resolvePttA11yBucket(
          enabled: false,
          active: false,
          serverPttLocked: true,
          pttLockSec: 5,
        ),
        PttA11yBucket.countdown,
      );
    });

    test('locked when server lock without countdown', () {
      expect(
        resolvePttA11yBucket(
          enabled: false,
          active: false,
          serverPttLocked: true,
          pttLockSec: 0,
        ),
        PttA11yBucket.locked,
      );
    });

    test('hold when enabled and idle', () {
      expect(
        resolvePttA11yBucket(
          enabled: true,
          active: false,
          serverPttLocked: false,
          pttLockSec: 0,
        ),
        PttA11yBucket.hold,
      );
    });
  });

  group('pttA11yLabelFor', () {
    test('freezes countdown label while focused', () {
      expect(
        pttA11yLabelFor(
          bucket: PttA11yBucket.countdown,
          pttLockSec: 3,
          frozenCountdownLabel: A11yStrings.pttCountdown(7),
        ),
        A11yStrings.pttCountdown(7),
      );
    });
  });
}
