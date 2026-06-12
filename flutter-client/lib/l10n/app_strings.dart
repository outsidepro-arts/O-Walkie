import 'package:flutter/widgets.dart';

import 'generated/app_localizations.dart';

export 'generated/app_localizations.dart';

/// UI strings backed by [AppLocalizations] (system locale en/ru).
///
/// [bind] is called from [MaterialApp.builder] so controllers and mappers
/// can read localized text without a [BuildContext].
abstract final class AppStrings {
  static AppLocalizations? _bound;

  static void bind(AppLocalizations l10n) => _bound = l10n;

  @visibleForTesting
  static AppLocalizations get l10n => _l;

  static AppLocalizations get _l {
    final bound = _bound;
    if (bound != null) return bound;
    return lookupAppLocalizations(
      WidgetsBinding.instance.platformDispatcher.locale,
    );
  }

  static String get appName => _l.appName;
  static String get menuMore => _l.menuMore;
  static String get menuRepeaterMode => _l.menuRepeaterMode;
  static String get connectionStateDisconnected => _l.connectionStateDisconnected;
  static String get connectionStateConnecting => _l.connectionStateConnecting;
  static String get connectionStateReconnecting => _l.connectionStateReconnecting;
  static String get connectionStatePausedPhoneCall =>
      _l.connectionStatePausedPhoneCall;
  static String get connectionStateConnected => _l.connectionStateConnected;
  static String get connectionStatePartial => _l.connectionStatePartial;
  static String get connectionStateProtocolIncompatible =>
      _l.connectionStateProtocolIncompatible;
  static String get connectionStateTransmitting => _l.connectionStateTransmitting;
  static String get connectionStateReceiving => _l.connectionStateReceiving;
  static String get connectionStateScanning => _l.connectionStateScanning;
  static String get connectionStateParallelTx => _l.connectionStateParallelTx;
  static String get connectionStateCalling => _l.connectionStateCalling;
  static String get connectionStateUnsupported => _l.connectionStateUnsupported;
  static String get signalQualityDefault => _l.signalQualityDefault;
  static String signalQualityPercent(int percent) =>
      _l.signalQualityPercent(percent);
  static String get signalRxActive => _l.signalRxActive;
  static String get signalRxBusy => _l.signalRxBusy;
  static String get serverProfiles => _l.serverProfiles;
  static String get collapseConnectionDetails => _l.collapseConnectionDetails;
  static String get expandConnectionDetails => _l.expandConnectionDetails;
  static String get previousServer => _l.previousServer;
  static String get nextServer => _l.nextServer;
  static String get connectServer => _l.connectServer;
  static String get disconnectServer => _l.disconnectServer;
  static String get scanToggle => _l.scanToggle;
  static String get scanning => _l.scanning;
  static String get serverNameLabel => _l.serverNameLabel;
  static String get serverNameHint => _l.serverNameHint;
  static String get serverHostLabel => _l.serverHostLabel;
  static String get serverHostHint => _l.serverHostHint;
  static String get portLabel => _l.portLabel;
  static String get portHint => _l.portHint;
  static String get channelLabel => _l.channelLabel;
  static String get channelHint => _l.channelHint;
  static String get saveServer => _l.saveServer;
  static String get deleteServer => _l.deleteServer;
  static String get moveServerUp => _l.moveServerUp;
  static String get moveServerDown => _l.moveServerDown;
  static String get shareConnection => _l.shareConnection;
  static String get importConnection => _l.importConnection;
  static String get connectionLinkCopied => _l.connectionLinkCopied;
  static String get connectionLinkIncludeNamePrompt =>
      _l.connectionLinkIncludeNamePrompt;
  static String get connectionLinkInvalid => _l.connectionLinkInvalid;
  static String get connectionLinkImported => _l.connectionLinkImported;
  static String get scanModeTitle => _l.scanModeTitle;
  static String get scanModeOneShot => _l.scanModeOneShot;
  static String get scanModeContinuous => _l.scanModeContinuous;
  static String get scanStartedAnnouncement => _l.scanStartedAnnouncement;
  static String get scanStoppedAnnouncement => _l.scanStoppedAnnouncement;
  static String scanFoundServerAnnouncement(String name) =>
      _l.scanFoundServerAnnouncement(name);
  static String scanFoundActivityToast(String name) =>
      _l.scanFoundActivityToast(name);
  static String get rxVolumeLabel => _l.rxVolumeLabel;
  static String get rxVolumeValueDefault => _l.rxVolumeValueDefault;
  static String rxVolumePercent(int percent) => _l.rxVolumePercent(percent);
  static String rxVolumePercentAccessibility(int percent) =>
      _l.rxVolumePercentAccessibility(percent);
  static String get pttHold => _l.pttHold;
  static String get pttStopTalking => _l.pttStopTalking;
  static String get pttUnavailable => _l.pttUnavailable;
  static String pttLockedCountdown(int sec) => _l.pttLockedCountdown(sec);
  static String pttTxCountdown(int sec) => _l.pttTxCountdown(sec);
  static String get callSignal => _l.callSignal;
  static String get coreVersionFooter => _l.coreVersionFooter;
  static String get protocolLabel => _l.protocolLabel;
  static String get settingsTitle => _l.settingsTitle;
  static String get settingsAudio => _l.settingsAudio;
  static String get settingsAudioInputDevice => _l.settingsAudioInputDevice;
  static String microphoneSourceTitle(String id, {required String fallback}) {
    switch (id) {
      case 'mic':
        return _l.microphoneSourceMic;
      case 'default':
        return _l.microphoneSourceDefault;
      case 'camcorder':
        return _l.microphoneSourceCamcorder;
      case 'voice_recognition':
        return _l.microphoneSourceVoiceRecognition;
      case 'voice_communication':
        return _l.microphoneSourceVoiceCommunication;
      case 'unprocessed':
        return _l.microphoneSourceUnprocessed;
      case 'voice_performance':
        return _l.microphoneSourceVoicePerformance;
      default:
        return fallback;
    }
  }
  static String get settingsAudioOutputDevice => _l.settingsAudioOutputDevice;
  static String get settingsAudioDeviceDefault => _l.settingsAudioDeviceDefault;
  static String get settingsPauseDuringPhoneCall =>
      _l.settingsPauseDuringPhoneCall;
  static String get settingsUseBluetoothHeadset => _l.settingsUseBluetoothHeadset;
  static String get settingsVibrationImitation => _l.settingsVibrationImitation;
  static String get settingsVibrationImitationFrequency =>
      _l.settingsVibrationImitationFrequency;
  static String get settingsVibrationImitationVolume =>
      _l.settingsVibrationImitationVolume;
  static String get settingsVibrationImitationPreview =>
      _l.settingsVibrationImitationPreview;
  static String get settingsExternalControl => _l.settingsExternalControl;
  static String get settingsWindows => _l.settingsWindows;
  static String get settingsGlobalPttHotkey => _l.settingsGlobalPttHotkey;
  static String get settingsGlobalPttHotkeyUnassigned =>
      _l.settingsGlobalPttHotkeyUnassigned;
  static String get settingsGlobalPttHotkeyAssign =>
      _l.settingsGlobalPttHotkeyAssign;
  static String get settingsGlobalPttHotkeyClear => _l.settingsGlobalPttHotkeyClear;
  static String get settingsGlobalPttHotkeyDialogTitle =>
      _l.settingsGlobalPttHotkeyDialogTitle;
  static String get settingsGlobalPttHotkeyDialogHint =>
      _l.settingsGlobalPttHotkeyDialogHint;
  static String get settingsGlobalPttHotkeyDialogWaiting =>
      _l.settingsGlobalPttHotkeyDialogWaiting;
  static String get settingsMinimizeToTray => _l.settingsMinimizeToTray;
  static String get trayMenuShow => _l.trayMenuShow;
  static String get trayMenuExit => _l.trayMenuExit;
  static String get settingsMediaButtonPtt => _l.settingsMediaButtonPtt;
  static String get settingsHardwarePttKey => _l.settingsHardwarePttKey;
  static String get settingsHardwarePttUnassigned =>
      _l.settingsHardwarePttUnassigned;
  static String get settingsHardwarePttAssign => _l.settingsHardwarePttAssign;
  static String get settingsHardwarePttDialogTitle =>
      _l.settingsHardwarePttDialogTitle;
  static String get settingsHardwarePttDialogWaiting =>
      _l.settingsHardwarePttDialogWaiting;
  static String get settingsHardwarePttReset => _l.settingsHardwarePttReset;
  static String get settingsDisplay => _l.settingsDisplay;
  static String get settingsOrientation => _l.settingsOrientation;
  static String get settingsAbout => _l.settingsAbout;
  static String get settingsAppVersion => _l.settingsAppVersion;
  static String get settingsProtocolVersion => _l.settingsProtocolVersion;
  static String get settingsGitHub => _l.settingsGitHub;
  static String get settingsGitHubOpenFailed => _l.settingsGitHubOpenFailed;
  static String get orientationFollowSystem => _l.orientationFollowSystem;
  static String get orientationPortrait => _l.orientationPortrait;
  static String get orientationLandscape => _l.orientationLandscape;
  static String get menuSettings => _l.menuSettings;
  static String get cannotSwitchProfileConnected =>
      _l.cannotSwitchProfileConnected;
  static String get cannotDeleteLastProfile => _l.cannotDeleteLastProfile;
  static String get profileSaved => _l.profileSaved;
  static String get rogerSignalLabel => _l.rogerSignalLabel;
  static String get callSignalLabel => _l.callSignalLabel;
  static String get rogerCustomButton => _l.rogerCustomButton;
  static String get rogerCustomTitle => _l.rogerCustomTitle;
  static String get callCustomTitle => _l.callCustomTitle;
  static String get rogerNameHint => _l.rogerNameHint;
  static String get rogerPointsLabel => _l.rogerPointsLabel;
  static String get rogerNewSegment => _l.rogerNewSegment;
  static String get rogerEditSegment => _l.rogerEditSegment;
  static String get rogerFrequencyHint => _l.rogerFrequencyHint;
  static String get rogerDurationHint => _l.rogerDurationHint;
  static String get rogerPointInvalid => _l.rogerPointInvalid;
  static String get rogerPointsTooLong => _l.rogerPointsTooLong;
  static String get rogerPointPause => _l.rogerPointPause;
  static String rogerPointHz(int hz) => _l.rogerPointHz(hz);
  static String rogerPointDurationMs(int ms) => _l.rogerPointDurationMs(ms);
  static String get rogerSave => _l.rogerSave;
  static String get rogerCancel => _l.rogerCancel;
  static String get rogerPointsEmpty => _l.rogerPointsEmpty;
  static String get rogerNameRequired => _l.rogerNameRequired;
  static String get rogerPointsRequired => _l.rogerPointsRequired;
  static String get callRepeatLabel => _l.callRepeatLabel;
  static String get playSignalButton => _l.playSignalButton;
  static String get patternCopy => _l.patternCopy;
  static String get patternPaste => _l.patternPaste;
  static String get patternCopied => _l.patternCopied;
  static String get patternPasteFailed => _l.patternPasteFailed;
  static String get commonYes => _l.commonYes;
  static String get commonNo => _l.commonNo;
  static String get commonOk => _l.commonOk;
  static String profileNumberFallback(int number) =>
      _l.profileNumberFallback(number);
}
