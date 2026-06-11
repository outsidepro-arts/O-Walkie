import 'package:owalkie_core/owalkie_core.dart';

/// App-level alias for Kotlin [UiSignalPlayer].
abstract final class UiSignalPlayer {
  static Future<void> ensureLoaded() => UiSoundLibrary.ensureLoaded();

  static void playPttPress(SessionService? session) =>
      UiSoundLibrary.playPttPress(session);

  static void playPttRelease(SessionService? session) =>
      UiSoundLibrary.playPttRelease(session);

  static void playSwitch(SessionService? session) =>
      UiSoundLibrary.playSwitch(session);

  static void playVolumePreview(SessionService? session, int volumePercent) =>
      UiSoundLibrary.playVolumePreview(session, volumePercent);

  static void playConnected(SessionService? session) =>
      UiSoundLibrary.playConnected(session);

  static void playConnectionError(SessionService? session) =>
      UiSoundLibrary.playConnectionError(session);

  static void playManualConnectStart(SessionService? session) =>
      UiSoundLibrary.playManualConnectStart(session);

  static void playManualDisconnect(SessionService? session) =>
      UiSoundLibrary.playManualDisconnect(session);

  static void loadSoundBank(SessionService? session) {
    if (session == null || !session.isRunning) {
      return;
    }
    session.loadSoundBank(
      pttPress: UiSoundLibrary.pttPressSamples,
      pttRelease: UiSoundLibrary.pttReleaseSamples,
    );
  }
}
