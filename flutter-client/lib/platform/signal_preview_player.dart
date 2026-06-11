import 'package:owalkie_core/owalkie_core.dart';

import '../domain/signal_pattern.dart';

/// Kotlin [SignalPreviewPlayer] — local Roger/call preview in settings/editors.
abstract final class SignalPreviewPlayer {
  static void playPattern(
    SessionService? session,
    List<SignalPoint> points,
  ) {
    if (points.isEmpty) {
      return;
    }
    UiSoundLibrary.playSignalPatternPreview(
      session,
      [
        for (final p in points)
          (freqHz: p.freqHz, durationMs: p.durationMs),
      ],
    );
  }
}
