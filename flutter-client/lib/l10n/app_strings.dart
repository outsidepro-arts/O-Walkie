/// UI strings (mirrors Android `res/values/strings.xml`).
/// Later: migrate to `flutter gen-l10n` + ARB (en/ru).
abstract final class AppStrings {
  static const appName = 'O-Walkie';
  static const menuMore = 'More';
  static const menuRepeaterMode = 'Repeater mode';
  static const connectionStateDisconnected = 'Disconnected';
  static const connectionStateConnecting = 'Connecting…';
  static const connectionStateReconnecting = 'Reconnecting…';
  static const connectionStateConnected = 'Connected';
  static const connectionStateParallelTx = 'Parallel transmission!';
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
  static const rxVolumeLabel = 'Incoming volume';
  static const rxVolumeValueDefault = '100%';
  static const pttHold = 'Hold to Talk';
  static const pttActive = 'Talking…';

  static String pttLockedCountdown(int sec) => 'Locked $sec s';

  static String pttTxCountdown(int sec) => 'Wait $sec s';
  static const callSignal = 'Call';
  static const coreVersionFooter = 'Core';
  static const settingsTitle = 'Settings';
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
}
