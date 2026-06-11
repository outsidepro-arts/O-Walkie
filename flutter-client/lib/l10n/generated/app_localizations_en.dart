// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class AppLocalizationsEn extends AppLocalizations {
  AppLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get appName => 'O-Walkie';

  @override
  String get menuMore => 'More';

  @override
  String get menuRepeaterMode => 'Repeater mode';

  @override
  String get menuSettings => 'Settings';

  @override
  String get connectionStateDisconnected => 'Disconnected';

  @override
  String get connectionStateConnecting => 'Connecting…';

  @override
  String get connectionStateReconnecting => 'Reconnecting…';

  @override
  String get connectionStatePausedPhoneCall => 'Paused (phone call)';

  @override
  String get connectionStateConnected => 'Connected';

  @override
  String get connectionStatePartial => 'Connected (UDP init)';

  @override
  String get connectionStateProtocolIncompatible => 'Protocol incompatible';

  @override
  String get connectionStateTransmitting => 'Transmitting';

  @override
  String get connectionStateReceiving => 'Receiving';

  @override
  String get connectionStateScanning => 'Scanning...';

  @override
  String get connectionStateParallelTx => 'Parallel transmission!';

  @override
  String get connectionStateCalling => 'Calling…';

  @override
  String get connectionStateUnsupported => 'Session unavailable';

  @override
  String get signalQualityDefault => 'Signal: —';

  @override
  String signalQualityPercent(int percent) {
    return 'Signal: $percent%';
  }

  @override
  String get signalRxActive => 'RX active';

  @override
  String get signalRxBusy => 'RX busy';

  @override
  String get serverProfiles => 'Connection list';

  @override
  String get collapseConnectionDetails => 'Collapse connection details';

  @override
  String get expandConnectionDetails => 'Show connection details';

  @override
  String get previousServer => 'Previous';

  @override
  String get nextServer => 'Next';

  @override
  String get connectServer => 'Connect';

  @override
  String get disconnectServer => 'Disconnect';

  @override
  String get scanToggle => 'Scan';

  @override
  String get scanning => 'Scanning...';

  @override
  String get serverNameLabel => 'Server name';

  @override
  String get serverNameHint => 'Server name (example: Team Alpha)';

  @override
  String get serverHostLabel => 'Server host';

  @override
  String get serverHostHint => 'Server host/IP';

  @override
  String get portLabel => 'Port';

  @override
  String get portHint => 'Port';

  @override
  String get channelLabel => 'Channel';

  @override
  String get channelHint => 'Channel name';

  @override
  String get saveServer => 'Save server';

  @override
  String get deleteServer => 'Delete server';

  @override
  String get moveServerUp => 'Move up';

  @override
  String get moveServerDown => 'Move down';

  @override
  String get shareConnection => 'Share';

  @override
  String get importConnection => 'Import from clipboard';

  @override
  String get connectionLinkCopied => 'Connection link copied';

  @override
  String get connectionLinkIncludeNamePrompt =>
      'Add your connection name to the link?';

  @override
  String get connectionLinkInvalid =>
      'No valid O-Walkie connection link in clipboard';

  @override
  String get connectionLinkImported => 'Connection imported from link';

  @override
  String get scanModeTitle => 'Scan mode';

  @override
  String get scanModeOneShot => 'One-time';

  @override
  String get scanModeContinuous => 'Continuous';

  @override
  String get scanStartedAnnouncement => 'Scanning started';

  @override
  String get scanStoppedAnnouncement => 'Scanning stopped';

  @override
  String scanFoundServerAnnouncement(String name) {
    return 'Activity found on $name, connecting';
  }

  @override
  String scanFoundActivityToast(String name) {
    return 'Activity detected on $name';
  }

  @override
  String get rxVolumeLabel => 'Incoming volume';

  @override
  String get rxVolumeValueDefault => '100%';

  @override
  String rxVolumePercent(int percent) {
    return '$percent%';
  }

  @override
  String rxVolumePercentAccessibility(int percent) {
    return '$percent percent';
  }

  @override
  String get pttHold => 'Hold or tap to talk';

  @override
  String get pttStopTalking => 'Stop talking';

  @override
  String get pttUnavailable => 'PTT unavailable: no connection';

  @override
  String pttLockedCountdown(int sec) {
    return 'PTT in $sec s';
  }

  @override
  String pttTxCountdown(int sec) {
    return 'Wait $sec s';
  }

  @override
  String get callSignal => 'Call';

  @override
  String get coreVersionFooter => 'Core';

  @override
  String get protocolLabel => 'protocol';

  @override
  String get settingsTitle => 'Settings';

  @override
  String get settingsAudio => 'Audio';

  @override
  String get settingsPauseDuringPhoneCall => 'Pause relay during phone call';

  @override
  String get settingsUseBluetoothHeadset => 'Use Bluetooth headset';

  @override
  String get settingsExternalControl => 'Allow external control (Tasker)';

  @override
  String get settingsWindows => 'Windows';

  @override
  String get settingsGlobalPttHotkey => 'Global PTT hotkey';

  @override
  String get settingsGlobalPttHotkeyUnassigned => 'Not assigned';

  @override
  String get settingsGlobalPttHotkeyAssign => 'Assign';

  @override
  String get settingsGlobalPttHotkeyClear => 'Clear';

  @override
  String get settingsGlobalPttHotkeyDialogTitle => 'Press global PTT hotkey';

  @override
  String get settingsGlobalPttHotkeyDialogHint =>
      'Hold to transmit; release to stop (works when app is in background).';

  @override
  String get settingsGlobalPttHotkeyDialogWaiting =>
      'Press any key or key combination (Ctrl/Alt/Shift).';

  @override
  String get settingsMinimizeToTray =>
      'Minimize to system tray when closing the window';

  @override
  String get trayMenuShow => 'Show O-Walkie';

  @override
  String get trayMenuExit => 'Exit';

  @override
  String get settingsMediaButtonPtt => 'Headset play/pause toggles transmit';

  @override
  String get settingsHardwarePttKey => 'Hardware PTT key';

  @override
  String get settingsHardwarePttUnassigned => 'Not assigned';

  @override
  String get settingsHardwarePttAssign => 'Assign key';

  @override
  String get settingsHardwarePttDialogTitle => 'Press hardware PTT key';

  @override
  String get settingsHardwarePttDialogWaiting => 'Waiting for key press…';

  @override
  String get settingsHardwarePttReset => 'Reset';

  @override
  String get settingsDisplay => 'Display';

  @override
  String get settingsOrientation => 'Screen orientation';

  @override
  String get settingsAbout => 'About';

  @override
  String get settingsAppVersion => 'App version';

  @override
  String get settingsProtocolVersion => 'Protocol version';

  @override
  String get settingsGitHub => 'GitHub repository';

  @override
  String get settingsGitHubOpenFailed => 'Could not open GitHub';

  @override
  String get orientationFollowSystem => 'Follow system';

  @override
  String get orientationPortrait => 'Portrait';

  @override
  String get orientationLandscape => 'Landscape';

  @override
  String get cannotSwitchProfileConnected =>
      'Disconnect before switching connection profile.';

  @override
  String get cannotDeleteLastProfile => 'At least one profile is required.';

  @override
  String get profileSaved => 'Connection profile saved.';

  @override
  String get rogerSignalLabel => 'Roger signal';

  @override
  String get callSignalLabel => 'Call signal';

  @override
  String get rogerCustomButton => 'Custom';

  @override
  String get rogerCustomTitle => 'Custom Roger signal';

  @override
  String get callCustomTitle => 'Custom call signal';

  @override
  String get rogerNameHint => 'Signal name';

  @override
  String get rogerPointsLabel => 'Segments';

  @override
  String get rogerNewSegment => 'New segment';

  @override
  String get rogerEditSegment => 'Edit segment';

  @override
  String get rogerFrequencyHint => 'Frequency (Hz)';

  @override
  String get rogerDurationHint => 'Duration (ms)';

  @override
  String get rogerPointInvalid =>
      'Enter valid segment values (frequency >= 0, duration > 0).';

  @override
  String get rogerPointsTooLong =>
      'Total signal length is too long for this mode.';

  @override
  String get rogerPointPause => 'Pause';

  @override
  String rogerPointHz(int hz) {
    return '$hz Hz';
  }

  @override
  String rogerPointDurationMs(int ms) {
    return '$ms ms';
  }

  @override
  String get rogerSave => 'Save';

  @override
  String get rogerCancel => 'Cancel';

  @override
  String get rogerPointsEmpty => 'No segments yet';

  @override
  String get rogerNameRequired => 'Enter signal name';

  @override
  String get rogerPointsRequired => 'Add at least one segment';

  @override
  String get callRepeatLabel => 'Repeat count';

  @override
  String get patternCopy => 'Copy pattern';

  @override
  String get patternPaste => 'Paste pattern';

  @override
  String get patternCopied => 'Pattern copied to clipboard';

  @override
  String get patternPasteFailed => 'Could not paste pattern from clipboard';

  @override
  String get commonYes => 'Yes';

  @override
  String get commonNo => 'No';

  @override
  String get commonOk => 'OK';

  @override
  String profileNumberFallback(int number) {
    return 'Profile $number';
  }

  @override
  String get a11yScanStateOn => 'On';

  @override
  String get a11yScanStateOff => 'Off';

  @override
  String get a11yPttHoldHint => 'Press and hold, or double tap to transmit.';

  @override
  String get a11yPttActiveHint =>
      'Transmitting. Release, double tap, or press Stop to end.';

  @override
  String get a11yPttUnavailable => 'PTT unavailable: not connected.';

  @override
  String get a11yPttLocked => 'PTT is locked while the channel is busy.';

  @override
  String a11yPttCountdown(int sec) {
    return 'PTT unlocks in $sec seconds';
  }

  @override
  String get a11yConnectUnavailableHint => 'Session unavailable.';

  @override
  String get a11yNotAvailableYet => 'Not available';

  @override
  String get a11yPttStartAction => 'Start talking';

  @override
  String get a11yPttStopAction => 'Stop talking';

  @override
  String get a11yPttLockedAnnouncement => 'Locked';

  @override
  String get a11yPttUnlockedAnnouncement => 'Unlocked';
}
