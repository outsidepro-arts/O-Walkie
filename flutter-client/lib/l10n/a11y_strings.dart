/// Screen reader labels and hints (not shown visually).
abstract final class A11yStrings {
  static const connectionStatus = 'Connection status';
  static const signalStatus = 'Signal status';
  static const errorLiveRegion = 'Error';
  static const serverProfilePicker = 'Active server profile';
  static const serverProfilePickerHint = 'Select active connection profile';
  static const expandDetailsHint = 'Shows host, port, and channel fields';
  static const collapseDetailsHint = 'Hides connection detail fields';
  static const connectHint = 'Connects to the relay server using the profile below';
  static const disconnectHint = 'Disconnects from the relay server';
  static const connectUnavailableHint =
      'Unavailable. Native session library failed to load or is not built.';
  static const notAvailableYet = 'Not available in this version';
  static const scanOnHint = 'Scanning servers. Double tap to stop';
  static const scanOffHint = 'Double tap to start scanning servers';
  static const rxVolumeHint = 'Adjust incoming audio volume from 0 to 200 percent';
  static const pttLabel = 'Push to talk';
  static const pttActiveLabel = 'Talking';
  static const pttHint =
      'Short tap latches transmit until next tap. Hold for classic push-to-talk. '
      'Screen reader: use Start talking and Stop talking actions';
  static const pttDisabledHint = 'Connect to a server before transmitting';
  static const pttLockedHint = 'Server locked push-to-talk. Wait for unlock';
  static const pttStartAction = 'Start talking';
  static const pttStopAction = 'Stop talking';
  static const callSignalHint = 'Call signal, not available in this version';
  static const menuMoreHint = 'Additional options and settings';
  static const coreVersionLabel = 'Application core version';
  static const mainScrollHint = 'Main screen';
}
