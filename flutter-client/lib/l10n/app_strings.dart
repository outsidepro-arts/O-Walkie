/// UI strings (mirrors Android `res/values/strings.xml`).
/// Later: migrate to `flutter gen-l10n` + ARB (en/ru).
abstract final class AppStrings {
  static const appName = 'O-Walkie';
  static const menuMore = 'More';
  static const connectionStateDisconnected = 'Disconnected';
  static const connectionStateConnecting = 'Connecting…';
  static const connectionStateReconnecting = 'Reconnecting…';
  static const connectionStateConnected = 'Connected';
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
}
