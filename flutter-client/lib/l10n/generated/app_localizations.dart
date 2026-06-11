import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'app_localizations_en.dart';
import 'app_localizations_ru.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of AppLocalizations
/// returned by `AppLocalizations.of(context)`.
///
/// Applications need to include `AppLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'generated/app_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: AppLocalizations.localizationsDelegates,
///   supportedLocales: AppLocalizations.supportedLocales,
///   home: MyApplicationHome(),
/// );
/// ```
///
/// ## Update pubspec.yaml
///
/// Please make sure to update your pubspec.yaml to include the following
/// packages:
///
/// ```yaml
/// dependencies:
///   # Internationalization support.
///   flutter_localizations:
///     sdk: flutter
///   intl: any # Use the pinned version from flutter_localizations
///
///   # Rest of dependencies
/// ```
///
/// ## iOS Applications
///
/// iOS applications define key application metadata, including supported
/// locales, in an Info.plist file that is built into the application bundle.
/// To configure the locales supported by your app, you’ll need to edit this
/// file.
///
/// First, open your project’s ios/Runner.xcworkspace Xcode workspace file.
/// Then, in the Project Navigator, open the Info.plist file under the Runner
/// project’s Runner folder.
///
/// Next, select the Information Property List item, select Add Item from the
/// Editor menu, then select Localizations from the pop-up menu.
///
/// Select and expand the newly-created Localizations item then, for each
/// locale your application supports, add a new item and select the locale
/// you wish to add from the pop-up menu in the Value field. This list should
/// be consistent with the languages listed in the AppLocalizations.supportedLocales
/// property.
abstract class AppLocalizations {
  AppLocalizations(String locale)
    : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static AppLocalizations of(BuildContext context) {
    return Localizations.of<AppLocalizations>(context, AppLocalizations)!;
  }

  static const LocalizationsDelegate<AppLocalizations> delegate =
      _AppLocalizationsDelegate();

  /// A list of this localizations delegate along with the default localizations
  /// delegates.
  ///
  /// Returns a list of localizations delegates containing this delegate along with
  /// GlobalMaterialLocalizations.delegate, GlobalCupertinoLocalizations.delegate,
  /// and GlobalWidgetsLocalizations.delegate.
  ///
  /// Additional delegates can be added by appending to this list in
  /// MaterialApp. This list does not have to be used at all if a custom list
  /// of delegates is preferred or required.
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates =
      <LocalizationsDelegate<dynamic>>[
        delegate,
        GlobalMaterialLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
      ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[
    Locale('en'),
    Locale('ru'),
  ];

  /// No description provided for @appName.
  ///
  /// In en, this message translates to:
  /// **'O-Walkie'**
  String get appName;

  /// No description provided for @menuMore.
  ///
  /// In en, this message translates to:
  /// **'More'**
  String get menuMore;

  /// No description provided for @menuRepeaterMode.
  ///
  /// In en, this message translates to:
  /// **'Repeater mode'**
  String get menuRepeaterMode;

  /// No description provided for @menuSettings.
  ///
  /// In en, this message translates to:
  /// **'Settings'**
  String get menuSettings;

  /// No description provided for @connectionStateDisconnected.
  ///
  /// In en, this message translates to:
  /// **'Disconnected'**
  String get connectionStateDisconnected;

  /// No description provided for @connectionStateConnecting.
  ///
  /// In en, this message translates to:
  /// **'Connecting…'**
  String get connectionStateConnecting;

  /// No description provided for @connectionStateReconnecting.
  ///
  /// In en, this message translates to:
  /// **'Reconnecting…'**
  String get connectionStateReconnecting;

  /// No description provided for @connectionStatePausedPhoneCall.
  ///
  /// In en, this message translates to:
  /// **'Paused (phone call)'**
  String get connectionStatePausedPhoneCall;

  /// No description provided for @connectionStateConnected.
  ///
  /// In en, this message translates to:
  /// **'Connected'**
  String get connectionStateConnected;

  /// No description provided for @connectionStatePartial.
  ///
  /// In en, this message translates to:
  /// **'Connected (UDP init)'**
  String get connectionStatePartial;

  /// No description provided for @connectionStateProtocolIncompatible.
  ///
  /// In en, this message translates to:
  /// **'Protocol incompatible'**
  String get connectionStateProtocolIncompatible;

  /// No description provided for @connectionStateTransmitting.
  ///
  /// In en, this message translates to:
  /// **'Transmitting'**
  String get connectionStateTransmitting;

  /// No description provided for @connectionStateReceiving.
  ///
  /// In en, this message translates to:
  /// **'Receiving'**
  String get connectionStateReceiving;

  /// No description provided for @connectionStateScanning.
  ///
  /// In en, this message translates to:
  /// **'Scanning...'**
  String get connectionStateScanning;

  /// No description provided for @connectionStateParallelTx.
  ///
  /// In en, this message translates to:
  /// **'Parallel transmission!'**
  String get connectionStateParallelTx;

  /// No description provided for @connectionStateCalling.
  ///
  /// In en, this message translates to:
  /// **'Calling…'**
  String get connectionStateCalling;

  /// No description provided for @connectionStateUnsupported.
  ///
  /// In en, this message translates to:
  /// **'Session unavailable'**
  String get connectionStateUnsupported;

  /// No description provided for @signalQualityDefault.
  ///
  /// In en, this message translates to:
  /// **'Signal: —'**
  String get signalQualityDefault;

  /// No description provided for @signalQualityPercent.
  ///
  /// In en, this message translates to:
  /// **'Signal: {percent}%'**
  String signalQualityPercent(int percent);

  /// No description provided for @signalRxActive.
  ///
  /// In en, this message translates to:
  /// **'RX active'**
  String get signalRxActive;

  /// No description provided for @signalRxBusy.
  ///
  /// In en, this message translates to:
  /// **'RX busy'**
  String get signalRxBusy;

  /// No description provided for @serverProfiles.
  ///
  /// In en, this message translates to:
  /// **'Connection list'**
  String get serverProfiles;

  /// No description provided for @collapseConnectionDetails.
  ///
  /// In en, this message translates to:
  /// **'Collapse connection details'**
  String get collapseConnectionDetails;

  /// No description provided for @expandConnectionDetails.
  ///
  /// In en, this message translates to:
  /// **'Show connection details'**
  String get expandConnectionDetails;

  /// No description provided for @previousServer.
  ///
  /// In en, this message translates to:
  /// **'Previous'**
  String get previousServer;

  /// No description provided for @nextServer.
  ///
  /// In en, this message translates to:
  /// **'Next'**
  String get nextServer;

  /// No description provided for @connectServer.
  ///
  /// In en, this message translates to:
  /// **'Connect'**
  String get connectServer;

  /// No description provided for @disconnectServer.
  ///
  /// In en, this message translates to:
  /// **'Disconnect'**
  String get disconnectServer;

  /// No description provided for @scanToggle.
  ///
  /// In en, this message translates to:
  /// **'Scan'**
  String get scanToggle;

  /// No description provided for @scanning.
  ///
  /// In en, this message translates to:
  /// **'Scanning...'**
  String get scanning;

  /// No description provided for @serverNameLabel.
  ///
  /// In en, this message translates to:
  /// **'Server name'**
  String get serverNameLabel;

  /// No description provided for @serverNameHint.
  ///
  /// In en, this message translates to:
  /// **'Server name (example: Team Alpha)'**
  String get serverNameHint;

  /// No description provided for @serverHostLabel.
  ///
  /// In en, this message translates to:
  /// **'Server host'**
  String get serverHostLabel;

  /// No description provided for @serverHostHint.
  ///
  /// In en, this message translates to:
  /// **'Server host/IP'**
  String get serverHostHint;

  /// No description provided for @portLabel.
  ///
  /// In en, this message translates to:
  /// **'Port'**
  String get portLabel;

  /// No description provided for @portHint.
  ///
  /// In en, this message translates to:
  /// **'Port'**
  String get portHint;

  /// No description provided for @channelLabel.
  ///
  /// In en, this message translates to:
  /// **'Channel'**
  String get channelLabel;

  /// No description provided for @channelHint.
  ///
  /// In en, this message translates to:
  /// **'Channel name'**
  String get channelHint;

  /// No description provided for @saveServer.
  ///
  /// In en, this message translates to:
  /// **'Save server'**
  String get saveServer;

  /// No description provided for @deleteServer.
  ///
  /// In en, this message translates to:
  /// **'Delete server'**
  String get deleteServer;

  /// No description provided for @moveServerUp.
  ///
  /// In en, this message translates to:
  /// **'Move up'**
  String get moveServerUp;

  /// No description provided for @moveServerDown.
  ///
  /// In en, this message translates to:
  /// **'Move down'**
  String get moveServerDown;

  /// No description provided for @shareConnection.
  ///
  /// In en, this message translates to:
  /// **'Share'**
  String get shareConnection;

  /// No description provided for @importConnection.
  ///
  /// In en, this message translates to:
  /// **'Import from clipboard'**
  String get importConnection;

  /// No description provided for @connectionLinkCopied.
  ///
  /// In en, this message translates to:
  /// **'Connection link copied'**
  String get connectionLinkCopied;

  /// No description provided for @connectionLinkIncludeNamePrompt.
  ///
  /// In en, this message translates to:
  /// **'Add your connection name to the link?'**
  String get connectionLinkIncludeNamePrompt;

  /// No description provided for @connectionLinkInvalid.
  ///
  /// In en, this message translates to:
  /// **'No valid O-Walkie connection link in clipboard'**
  String get connectionLinkInvalid;

  /// No description provided for @connectionLinkImported.
  ///
  /// In en, this message translates to:
  /// **'Connection imported from link'**
  String get connectionLinkImported;

  /// No description provided for @scanModeTitle.
  ///
  /// In en, this message translates to:
  /// **'Scan mode'**
  String get scanModeTitle;

  /// No description provided for @scanModeOneShot.
  ///
  /// In en, this message translates to:
  /// **'One-time'**
  String get scanModeOneShot;

  /// No description provided for @scanModeContinuous.
  ///
  /// In en, this message translates to:
  /// **'Continuous'**
  String get scanModeContinuous;

  /// No description provided for @scanStartedAnnouncement.
  ///
  /// In en, this message translates to:
  /// **'Scanning started'**
  String get scanStartedAnnouncement;

  /// No description provided for @scanStoppedAnnouncement.
  ///
  /// In en, this message translates to:
  /// **'Scanning stopped'**
  String get scanStoppedAnnouncement;

  /// No description provided for @scanFoundServerAnnouncement.
  ///
  /// In en, this message translates to:
  /// **'Activity found on {name}, connecting'**
  String scanFoundServerAnnouncement(String name);

  /// No description provided for @scanFoundActivityToast.
  ///
  /// In en, this message translates to:
  /// **'Activity detected on {name}'**
  String scanFoundActivityToast(String name);

  /// No description provided for @rxVolumeLabel.
  ///
  /// In en, this message translates to:
  /// **'Incoming volume'**
  String get rxVolumeLabel;

  /// No description provided for @rxVolumeValueDefault.
  ///
  /// In en, this message translates to:
  /// **'100%'**
  String get rxVolumeValueDefault;

  /// No description provided for @rxVolumePercent.
  ///
  /// In en, this message translates to:
  /// **'{percent}%'**
  String rxVolumePercent(int percent);

  /// No description provided for @rxVolumePercentAccessibility.
  ///
  /// In en, this message translates to:
  /// **'{percent} percent'**
  String rxVolumePercentAccessibility(int percent);

  /// No description provided for @pttHold.
  ///
  /// In en, this message translates to:
  /// **'Hold to talk'**
  String get pttHold;

  /// No description provided for @pttStopTalking.
  ///
  /// In en, this message translates to:
  /// **'Stop talking'**
  String get pttStopTalking;

  /// No description provided for @pttUnavailable.
  ///
  /// In en, this message translates to:
  /// **'PTT unavailable: no connection'**
  String get pttUnavailable;

  /// No description provided for @pttLockedCountdown.
  ///
  /// In en, this message translates to:
  /// **'PTT in {sec} s'**
  String pttLockedCountdown(int sec);

  /// No description provided for @pttTxCountdown.
  ///
  /// In en, this message translates to:
  /// **'Wait {sec} s'**
  String pttTxCountdown(int sec);

  /// No description provided for @callSignal.
  ///
  /// In en, this message translates to:
  /// **'Call'**
  String get callSignal;

  /// No description provided for @coreVersionFooter.
  ///
  /// In en, this message translates to:
  /// **'Core'**
  String get coreVersionFooter;

  /// No description provided for @protocolLabel.
  ///
  /// In en, this message translates to:
  /// **'protocol'**
  String get protocolLabel;

  /// No description provided for @settingsTitle.
  ///
  /// In en, this message translates to:
  /// **'Settings'**
  String get settingsTitle;

  /// No description provided for @settingsAudio.
  ///
  /// In en, this message translates to:
  /// **'Audio'**
  String get settingsAudio;

  /// No description provided for @settingsPauseDuringPhoneCall.
  ///
  /// In en, this message translates to:
  /// **'Pause relay during phone call'**
  String get settingsPauseDuringPhoneCall;

  /// No description provided for @settingsUseBluetoothHeadset.
  ///
  /// In en, this message translates to:
  /// **'Use Bluetooth headset'**
  String get settingsUseBluetoothHeadset;

  /// No description provided for @settingsExternalControl.
  ///
  /// In en, this message translates to:
  /// **'Allow external control (Tasker)'**
  String get settingsExternalControl;

  /// No description provided for @settingsWindows.
  ///
  /// In en, this message translates to:
  /// **'Windows'**
  String get settingsWindows;

  /// No description provided for @settingsGlobalPttHotkey.
  ///
  /// In en, this message translates to:
  /// **'Global PTT hotkey'**
  String get settingsGlobalPttHotkey;

  /// No description provided for @settingsGlobalPttHotkeyUnassigned.
  ///
  /// In en, this message translates to:
  /// **'Not assigned'**
  String get settingsGlobalPttHotkeyUnassigned;

  /// No description provided for @settingsGlobalPttHotkeyAssign.
  ///
  /// In en, this message translates to:
  /// **'Assign'**
  String get settingsGlobalPttHotkeyAssign;

  /// No description provided for @settingsGlobalPttHotkeyClear.
  ///
  /// In en, this message translates to:
  /// **'Clear'**
  String get settingsGlobalPttHotkeyClear;

  /// No description provided for @settingsGlobalPttHotkeyDialogTitle.
  ///
  /// In en, this message translates to:
  /// **'Press global PTT hotkey'**
  String get settingsGlobalPttHotkeyDialogTitle;

  /// No description provided for @settingsGlobalPttHotkeyDialogHint.
  ///
  /// In en, this message translates to:
  /// **'Hold to transmit; release to stop (works when app is in background).'**
  String get settingsGlobalPttHotkeyDialogHint;

  /// No description provided for @settingsGlobalPttHotkeyDialogWaiting.
  ///
  /// In en, this message translates to:
  /// **'Press any key or key combination (Ctrl/Alt/Shift).'**
  String get settingsGlobalPttHotkeyDialogWaiting;

  /// No description provided for @settingsMinimizeToTray.
  ///
  /// In en, this message translates to:
  /// **'Minimize to system tray when closing the window'**
  String get settingsMinimizeToTray;

  /// No description provided for @trayMenuShow.
  ///
  /// In en, this message translates to:
  /// **'Show O-Walkie'**
  String get trayMenuShow;

  /// No description provided for @trayMenuExit.
  ///
  /// In en, this message translates to:
  /// **'Exit'**
  String get trayMenuExit;

  /// No description provided for @settingsMediaButtonPtt.
  ///
  /// In en, this message translates to:
  /// **'Headset play/pause toggles transmit'**
  String get settingsMediaButtonPtt;

  /// No description provided for @settingsHardwarePttKey.
  ///
  /// In en, this message translates to:
  /// **'Hardware PTT key'**
  String get settingsHardwarePttKey;

  /// No description provided for @settingsHardwarePttUnassigned.
  ///
  /// In en, this message translates to:
  /// **'Not assigned'**
  String get settingsHardwarePttUnassigned;

  /// No description provided for @settingsHardwarePttAssign.
  ///
  /// In en, this message translates to:
  /// **'Assign key'**
  String get settingsHardwarePttAssign;

  /// No description provided for @settingsHardwarePttDialogTitle.
  ///
  /// In en, this message translates to:
  /// **'Press hardware PTT key'**
  String get settingsHardwarePttDialogTitle;

  /// No description provided for @settingsHardwarePttDialogWaiting.
  ///
  /// In en, this message translates to:
  /// **'Waiting for key press…'**
  String get settingsHardwarePttDialogWaiting;

  /// No description provided for @settingsHardwarePttReset.
  ///
  /// In en, this message translates to:
  /// **'Reset'**
  String get settingsHardwarePttReset;

  /// No description provided for @settingsDisplay.
  ///
  /// In en, this message translates to:
  /// **'Display'**
  String get settingsDisplay;

  /// No description provided for @settingsOrientation.
  ///
  /// In en, this message translates to:
  /// **'Screen orientation'**
  String get settingsOrientation;

  /// No description provided for @settingsAbout.
  ///
  /// In en, this message translates to:
  /// **'About'**
  String get settingsAbout;

  /// No description provided for @settingsAppVersion.
  ///
  /// In en, this message translates to:
  /// **'App version'**
  String get settingsAppVersion;

  /// No description provided for @settingsProtocolVersion.
  ///
  /// In en, this message translates to:
  /// **'Protocol version'**
  String get settingsProtocolVersion;

  /// No description provided for @settingsGitHub.
  ///
  /// In en, this message translates to:
  /// **'GitHub repository'**
  String get settingsGitHub;

  /// No description provided for @settingsGitHubOpenFailed.
  ///
  /// In en, this message translates to:
  /// **'Could not open GitHub'**
  String get settingsGitHubOpenFailed;

  /// No description provided for @orientationFollowSystem.
  ///
  /// In en, this message translates to:
  /// **'Follow system'**
  String get orientationFollowSystem;

  /// No description provided for @orientationPortrait.
  ///
  /// In en, this message translates to:
  /// **'Portrait'**
  String get orientationPortrait;

  /// No description provided for @orientationLandscape.
  ///
  /// In en, this message translates to:
  /// **'Landscape'**
  String get orientationLandscape;

  /// No description provided for @cannotSwitchProfileConnected.
  ///
  /// In en, this message translates to:
  /// **'Disconnect before switching connection profile.'**
  String get cannotSwitchProfileConnected;

  /// No description provided for @cannotDeleteLastProfile.
  ///
  /// In en, this message translates to:
  /// **'At least one profile is required.'**
  String get cannotDeleteLastProfile;

  /// No description provided for @profileSaved.
  ///
  /// In en, this message translates to:
  /// **'Connection profile saved.'**
  String get profileSaved;

  /// No description provided for @rogerSignalLabel.
  ///
  /// In en, this message translates to:
  /// **'Roger signal'**
  String get rogerSignalLabel;

  /// No description provided for @callSignalLabel.
  ///
  /// In en, this message translates to:
  /// **'Call signal'**
  String get callSignalLabel;

  /// No description provided for @rogerCustomButton.
  ///
  /// In en, this message translates to:
  /// **'Custom'**
  String get rogerCustomButton;

  /// No description provided for @rogerCustomTitle.
  ///
  /// In en, this message translates to:
  /// **'Custom Roger signal'**
  String get rogerCustomTitle;

  /// No description provided for @callCustomTitle.
  ///
  /// In en, this message translates to:
  /// **'Custom call signal'**
  String get callCustomTitle;

  /// No description provided for @rogerNameHint.
  ///
  /// In en, this message translates to:
  /// **'Signal name'**
  String get rogerNameHint;

  /// No description provided for @rogerPointsLabel.
  ///
  /// In en, this message translates to:
  /// **'Segments'**
  String get rogerPointsLabel;

  /// No description provided for @rogerNewSegment.
  ///
  /// In en, this message translates to:
  /// **'New segment'**
  String get rogerNewSegment;

  /// No description provided for @rogerEditSegment.
  ///
  /// In en, this message translates to:
  /// **'Edit segment'**
  String get rogerEditSegment;

  /// No description provided for @rogerFrequencyHint.
  ///
  /// In en, this message translates to:
  /// **'Frequency (Hz)'**
  String get rogerFrequencyHint;

  /// No description provided for @rogerDurationHint.
  ///
  /// In en, this message translates to:
  /// **'Duration (ms)'**
  String get rogerDurationHint;

  /// No description provided for @rogerPointInvalid.
  ///
  /// In en, this message translates to:
  /// **'Enter valid segment values (frequency >= 0, duration > 0).'**
  String get rogerPointInvalid;

  /// No description provided for @rogerPointsTooLong.
  ///
  /// In en, this message translates to:
  /// **'Total signal length is too long for this mode.'**
  String get rogerPointsTooLong;

  /// No description provided for @rogerPointPause.
  ///
  /// In en, this message translates to:
  /// **'Pause'**
  String get rogerPointPause;

  /// No description provided for @rogerPointHz.
  ///
  /// In en, this message translates to:
  /// **'{hz} Hz'**
  String rogerPointHz(int hz);

  /// No description provided for @rogerPointDurationMs.
  ///
  /// In en, this message translates to:
  /// **'{ms} ms'**
  String rogerPointDurationMs(int ms);

  /// No description provided for @rogerSave.
  ///
  /// In en, this message translates to:
  /// **'Save'**
  String get rogerSave;

  /// No description provided for @rogerCancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get rogerCancel;

  /// No description provided for @rogerPointsEmpty.
  ///
  /// In en, this message translates to:
  /// **'No segments yet'**
  String get rogerPointsEmpty;

  /// No description provided for @rogerNameRequired.
  ///
  /// In en, this message translates to:
  /// **'Enter signal name'**
  String get rogerNameRequired;

  /// No description provided for @rogerPointsRequired.
  ///
  /// In en, this message translates to:
  /// **'Add at least one segment'**
  String get rogerPointsRequired;

  /// No description provided for @callRepeatLabel.
  ///
  /// In en, this message translates to:
  /// **'Repeat count'**
  String get callRepeatLabel;

  /// No description provided for @playSignalButton.
  ///
  /// In en, this message translates to:
  /// **'Play'**
  String get playSignalButton;

  /// No description provided for @patternCopy.
  ///
  /// In en, this message translates to:
  /// **'Copy pattern'**
  String get patternCopy;

  /// No description provided for @patternPaste.
  ///
  /// In en, this message translates to:
  /// **'Paste pattern'**
  String get patternPaste;

  /// No description provided for @patternCopied.
  ///
  /// In en, this message translates to:
  /// **'Pattern copied to clipboard'**
  String get patternCopied;

  /// No description provided for @patternPasteFailed.
  ///
  /// In en, this message translates to:
  /// **'Could not paste pattern from clipboard'**
  String get patternPasteFailed;

  /// No description provided for @commonYes.
  ///
  /// In en, this message translates to:
  /// **'Yes'**
  String get commonYes;

  /// No description provided for @commonNo.
  ///
  /// In en, this message translates to:
  /// **'No'**
  String get commonNo;

  /// No description provided for @commonOk.
  ///
  /// In en, this message translates to:
  /// **'OK'**
  String get commonOk;

  /// No description provided for @profileNumberFallback.
  ///
  /// In en, this message translates to:
  /// **'Profile {number}'**
  String profileNumberFallback(int number);

  /// No description provided for @a11yScanStateOn.
  ///
  /// In en, this message translates to:
  /// **'On'**
  String get a11yScanStateOn;

  /// No description provided for @a11yScanStateOff.
  ///
  /// In en, this message translates to:
  /// **'Off'**
  String get a11yScanStateOff;

  /// No description provided for @a11yPttHoldHint.
  ///
  /// In en, this message translates to:
  /// **'Press and hold to transmit, or use the Start talking action.'**
  String get a11yPttHoldHint;

  /// No description provided for @a11yPttActiveHint.
  ///
  /// In en, this message translates to:
  /// **'Transmitting. Use the Stop talking action to end.'**
  String get a11yPttActiveHint;

  /// No description provided for @a11yPttUnavailable.
  ///
  /// In en, this message translates to:
  /// **'PTT unavailable: not connected.'**
  String get a11yPttUnavailable;

  /// No description provided for @a11yPttLocked.
  ///
  /// In en, this message translates to:
  /// **'PTT is locked while the channel is busy.'**
  String get a11yPttLocked;

  /// No description provided for @a11yPttCountdown.
  ///
  /// In en, this message translates to:
  /// **'PTT unlocks in {sec} seconds'**
  String a11yPttCountdown(int sec);

  /// No description provided for @a11yConnectUnavailableHint.
  ///
  /// In en, this message translates to:
  /// **'Session unavailable.'**
  String get a11yConnectUnavailableHint;

  /// No description provided for @a11yNotAvailableYet.
  ///
  /// In en, this message translates to:
  /// **'Not available'**
  String get a11yNotAvailableYet;

  /// No description provided for @a11yPttStartAction.
  ///
  /// In en, this message translates to:
  /// **'Start talking'**
  String get a11yPttStartAction;

  /// No description provided for @a11yPttStopAction.
  ///
  /// In en, this message translates to:
  /// **'Stop talking'**
  String get a11yPttStopAction;

  /// No description provided for @a11yPttLockedAnnouncement.
  ///
  /// In en, this message translates to:
  /// **'Locked'**
  String get a11yPttLockedAnnouncement;

  /// No description provided for @a11yPttUnlockedAnnouncement.
  ///
  /// In en, this message translates to:
  /// **'Unlocked'**
  String get a11yPttUnlockedAnnouncement;
}

class _AppLocalizationsDelegate
    extends LocalizationsDelegate<AppLocalizations> {
  const _AppLocalizationsDelegate();

  @override
  Future<AppLocalizations> load(Locale locale) {
    return SynchronousFuture<AppLocalizations>(lookupAppLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) =>
      <String>['en', 'ru'].contains(locale.languageCode);

  @override
  bool shouldReload(_AppLocalizationsDelegate old) => false;
}

AppLocalizations lookupAppLocalizations(Locale locale) {
  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en':
      return AppLocalizationsEn();
    case 'ru':
      return AppLocalizationsRu();
  }

  throw FlutterError(
    'AppLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.',
  );
}
