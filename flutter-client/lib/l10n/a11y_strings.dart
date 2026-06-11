import 'package:flutter/widgets.dart';

import 'app_strings.dart';
import 'generated/app_localizations.dart';

/// Screen reader labels and hints (not shown visually).
abstract final class A11yStrings {
  static void bind(AppLocalizations l10n) => AppStrings.bind(l10n);

  static AppLocalizations get _l => AppStrings.l10n;

  static String get scanStateOn => _l.a11yScanStateOn;
  static String get scanStateOff => _l.a11yScanStateOff;
  static String get pttHoldHint => _l.a11yPttHoldHint;
  static String get pttToggleHint => _l.a11yPttToggleHint;
  static String get pttUnavailable => _l.a11yPttUnavailable;
  static String get pttLocked => _l.a11yPttLocked;
  static String pttCountdown(int sec) => _l.a11yPttCountdown(sec);
  static String get connectUnavailableHint => _l.a11yConnectUnavailableHint;
  static String get notAvailableYet => _l.a11yNotAvailableYet;
  static String get pttStartAction => _l.a11yPttStartAction;
  static String get pttStopAction => _l.a11yPttStopAction;
}
