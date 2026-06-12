/// Shared vibrator timing definitions (mobile API + desktop synthesis source).
abstract final class VibrationPatterns {
  /// Android [Vibration.vibrate] pattern: initial delay, on, off, …
  static const parallelTxCollision = [0, 26, 74];

  static const transmitTimeoutDurationMs = 50;
  static const scanActivityDurationMs = 200;

  /// Windows wx settings preview ([AudioEngine::PlayVibrationImitationPreview]).
  static const preview = [35, 55, 35, 55, 35];
}

/// User-tunable desktop imitation parameters (Windows wx defaults).
final class VibrationImitationSettings {
  const VibrationImitationSettings({
    required this.freqHz,
    required this.volumePercent,
  });

  const VibrationImitationSettings.defaults()
      : freqHz = 100.0,
        volumePercent = 40;

  final double freqHz;
  final int volumePercent;
}
