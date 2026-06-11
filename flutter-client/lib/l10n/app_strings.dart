/// UI strings (mirrors Android `res/values/strings.xml`).
/// Later: migrate to `flutter gen-l10n` + ARB (en/ru).
abstract final class AppStrings {
  static const appName = 'O-Walkie';
  static const menuMore = 'More';
  static const menuRepeaterMode = 'Repeater mode';
  static const connectionStateDisconnected = 'Disconnected';
  static const connectionStateConnecting = 'Connecting…';
  static const connectionStateReconnecting = 'Reconnecting…';
  static const connectionStatePausedPhoneCall = 'Paused (phone call)';
  static const connectionStateConnected = 'Connected';
  static const connectionStateParallelTx = 'Parallel transmission!';
  static const connectionStateCalling = 'Calling…';
  static const connectionStateUnsupported = 'Session unavailable';
  static const signalQualityDefault = 'Signal: —';
  static const signalRxActive = 'RX active';
  static const signalRxBusy = 'RX busy';
  static const serverProfiles = 'Connection list';
  static const collapseConnectionDetails = 'Collapse connection details';
  static const expandConnectionDetails = 'Show connection details';
  static const previousServer = 'Previous';
  static const nextServer = 'Next';
  static const connectServer = 'Connect';
  static const disconnectServer = 'Disconnect';
  static const scanToggle = 'Scan';
  static const scanning = 'Scanning...';
  static const serverNameLabel = 'Server name';
  static const serverNameHint = 'Server name (example: Team Alpha)';
  static const serverHostLabel = 'Server host';
  static const serverHostHint = 'Server host/IP';
  static const portLabel = 'Port';
  static const portHint = 'Port';
  static const channelLabel = 'Channel';
  static const channelHint = 'Channel name';
  static const saveServer = 'Save server';
  static const deleteServer = 'Delete server';
  static const moveServerUp = 'Move up';
  static const moveServerDown = 'Move down';
  static const shareConnection = 'Share';
  static const importConnection = 'Import from clipboard';
  static const connectionLinkCopied = 'Connection link copied';
  static const connectionLinkIncludeNamePrompt =
      'Add your connection name to the link?';
  static const connectionLinkInvalid =
      'No valid O-Walkie connection link in clipboard';
  static const connectionLinkImported = 'Connection imported from link';
  static const scanModeTitle = 'Scan mode';
  static const scanModeOneShot = 'One-time';
  static const scanModeContinuous = 'Continuous';
  static const scanStartedAnnouncement = 'Scanning started';
  static const scanStoppedAnnouncement = 'Scanning stopped';
  static String scanFoundServerAnnouncement(String name) =>
      'Activity found on $name, connecting';
  static String scanFoundActivityToast(String name) =>
      'Activity detected on $name';
  static const rxVolumeLabel = 'Incoming volume';
  static const rxVolumeValueDefault = '100%';
  static const pttHold = 'Hold to Talk';
  static const pttActive = 'Talking…';

  static String pttLockedCountdown(int sec) => 'Locked $sec s';

  static String pttTxCountdown(int sec) => 'Wait $sec s';
  static const callSignal = 'Call';
  static const coreVersionFooter = 'Core';
  static const settingsTitle = 'Settings';
  static const settingsAudio = 'Audio';
  static const settingsPauseDuringPhoneCall = 'Pause relay during phone call';
  static const settingsUseBluetoothHeadset = 'Use Bluetooth headset';
  static const settingsExternalControl = 'Allow external control (Tasker)';
  static const settingsMediaButtonPtt = 'Headset play/pause toggles transmit';
  static const settingsHardwarePttKey = 'Hardware PTT key';
  static const settingsHardwarePttUnassigned = 'Not assigned';
  static const settingsHardwarePttAssign = 'Assign key';
  static const settingsHardwarePttDialogTitle = 'Press hardware PTT key';
  static const settingsHardwarePttDialogWaiting = 'Waiting for key press…';
  static const settingsHardwarePttReset = 'Reset';
  static const settingsDisplay = 'Display';
  static const settingsOrientation = 'Screen orientation';
  static const settingsAbout = 'About';
  static const settingsAppVersion = 'App version';
  static const settingsProtocolVersion = 'Protocol version';
  static const settingsGitHub = 'GitHub repository';
  static const orientationFollowSystem = 'Follow system';
  static const orientationPortrait = 'Portrait';
  static const orientationLandscape = 'Landscape';
  static const menuSettings = 'Settings';
  static const cannotSwitchProfileConnected =
      'Disconnect before switching connection profile.';
  static const cannotDeleteLastProfile = 'At least one profile is required.';
  static const profileSaved = 'Connection profile saved.';
  static const rogerSignalLabel = 'Roger signal';
  static const callSignalLabel = 'Call signal';
  static const rogerCustomButton = 'Custom';
  static const rogerCustomTitle = 'Custom Roger signal';
  static const callCustomTitle = 'Custom call signal';
  static const rogerNameHint = 'Signal name';
  static const rogerPointsLabel = 'Segments';
  static const rogerNewSegment = 'New segment';
  static const rogerEditSegment = 'Edit segment';
  static const rogerFrequencyHint = 'Frequency (Hz)';
  static const rogerDurationHint = 'Duration (ms)';
  static const rogerPointInvalid = 'Enter valid segment values (frequency >= 0, duration > 0).';
  static const rogerPointsTooLong = 'Total signal length is too long for this mode.';
  static const rogerPointPause = 'Pause';
  static String rogerPointHz(int hz) => '$hz Hz';
  static const rogerSave = 'Save';
  static const rogerCancel = 'Cancel';
  static const rogerPointsEmpty = 'No segments yet';
  static const rogerNameRequired = 'Enter signal name';
  static const rogerPointsRequired = 'Add at least one segment';
  static const callRepeatLabel = 'Repeat count';
  static const patternCopy = 'Copy pattern';
  static const patternPaste = 'Paste pattern';
  static const patternCopied = 'Pattern copied to clipboard';
  static const patternPasteFailed = 'Could not paste pattern from clipboard';
}
