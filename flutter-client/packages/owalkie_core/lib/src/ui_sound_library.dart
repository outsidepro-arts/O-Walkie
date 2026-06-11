import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/services.dart';

import '../session_service.dart';
import 'signal_pattern_pcm.dart';
import 'signal_point.dart';
import 'wav_pcm.dart';

/// Tone segment for Roger/call preview from the app layer (no worker [SignalPoint] leak).
typedef SignalPreviewSegment = ({double freqHz, int durationMs});

/// Kotlin [UiSignalPlayer] + [WalkieService] local UI tones.
///
/// WAV assets live in `packages/owalkie_core/assets/sounds/`.
abstract final class UiSoundLibrary {
  static const _packagePrefix = 'packages/owalkie_core/assets/sounds/';
  static const playbackSampleRate = 44100;

  static List<int>? _pttPress;
  static List<int>? _pttRelease;
  static List<int>? _switchNav;
  static List<int>? _volumePreview;
  static Future<void>? _loadFuture;

  static Future<void> ensureLoaded() {
    return _loadFuture ??= _loadAll();
  }

  static Future<void> _loadAll() async {
    _pttPress = await _loadResampled('selfpttup_002.wav');
    _pttRelease = await _loadResampled('selfttdown_002.wav');
    _switchNav = await _loadResampled('switch_nav.wav');
    _volumePreview = await _loadResampled('spin_wt.wav');
  }

  static Future<List<int>> _loadResampled(String fileName) async {
    final data = await rootBundle.load('$_packagePrefix$fileName');
    final wav = WavPcm.decode(data.buffer.asUint8List());
    if (wav.samples.isEmpty) {
      return const [];
    }
    return WavPcm.resampleLinear(
      wav.samples,
      wav.sampleRate,
      playbackSampleRate,
    );
  }

  static List<int> get pttPressSamples => _pttPress ?? const [];
  static List<int> get pttReleaseSamples => _pttRelease ?? const [];

  static void playPttPress(SessionService? session) {
    _playSamples(session, pttPressSamples);
  }

  static void playPttRelease(SessionService? session) {
    _playSamples(session, pttReleaseSamples);
  }

  static void playSwitch(SessionService? session) {
    _playSamples(session, _switchNav ?? const []);
  }

  static void playVolumePreview(SessionService? session, int volumePercent) {
    final pcm = _volumePreview;
    if (pcm == null || pcm.isEmpty) {
      return;
    }
    final safe = volumePercent.clamp(0, 200);
    _playSamples(session, WavPcm.applyGainPercent(pcm, safe));
  }

  /// Roger/call pattern preview (Kotlin [SignalPreviewPlayer] parity).
  static void playSignalPatternPreview(
    SessionService? session,
    List<SignalPreviewSegment> segments,
  ) {
    if (segments.isEmpty) {
      return;
    }
    final points = [
      for (final s in segments)
        SignalPoint(freqHz: s.freqHz, durationMs: s.durationMs),
    ];
    _playSamples(session, SignalPatternPcm.synthesize(points));
  }

  static void playConnected(SessionService? session) {
    _play(
      session,
      const [
        _Segment(1400, 40),
        _Segment(0, 50),
        _Segment(1700, 50),
      ],
      gain: 0.22,
    );
  }

  static void playConnectionError(SessionService? session) {
    _play(
      session,
      const [
        _Segment(890, 192),
        _Segment(0, 300),
        _Segment(890, 200),
      ],
      gain: 0.2,
    );
  }

  static void playManualConnectStart(SessionService? session) {
    _play(
      session,
      const [
        _Segment(932.33, 50),
        _Segment(1174.66, 50),
        _Segment(1396.91, 50),
        _Segment(1864.66, 70),
      ],
      gain: 0.22,
    );
  }

  static void playManualDisconnect(SessionService? session) {
    _play(
      session,
      const [
        _Segment(1864.66, 70),
        _Segment(1396.91, 50),
        _Segment(1174.66, 50),
        _Segment(932.33, 50),
      ],
      gain: 0.22,
    );
  }

  /// Prepends PTT release tone before roger local playback (Kotlin parity).
  static List<int> prependPttRelease(List<int> rogerLocal) {
    final release = pttReleaseSamples;
    if (release.isEmpty) {
      return rogerLocal;
    }
    if (rogerLocal.isEmpty) {
      return List<int>.from(release);
    }
    return [...release, ...rogerLocal];
  }

  static void _play(
    SessionService? session,
    List<_Segment> segments, {
    required double gain,
  }) {
    _playSamples(session, _synthesize(segments, gain: gain));
  }

  static void _playSamples(SessionService? session, List<int> samples) {
    if (samples.isEmpty || session == null || !session.isRunning) {
      return;
    }
    session.playLocalSamples(samples, sampleRate: playbackSampleRate);
  }

  static List<int> _synthesize(List<_Segment> segments, {required double gain}) {
    final total = segments.fold<int>(
      0,
      (sum, s) => sum + (playbackSampleRate * s.durationMs) ~/ 1000,
    );
    final out = List<int>.filled(total.clamp(1, total), 0);
    var idx = 0;
    var phase = 0.0;
    for (final seg in segments) {
      final n = ((playbackSampleRate * seg.durationMs) / 1000).clamp(1, 1 << 20);
      final step =
          seg.freqHz > 0 ? 2 * math.pi * seg.freqHz / playbackSampleRate : 0.0;
      for (var i = 0; i < n; i++) {
        if (idx >= out.length) {
          break;
        }
        final envPos = i / n;
        final env = envPos < 0.08
            ? envPos / 0.08
            : envPos > 0.92
                ? (1 - envPos) / 0.08
                : 1.0;
        final sample = seg.freqHz > 0 ? math.sin(phase) * env * gain : 0.0;
        out[idx] = (sample * 32767).round().clamp(-32768, 32767);
        idx++;
        phase += step;
      }
    }
    return idx == out.length ? out : out.sublist(0, idx);
  }
}

class _Segment {
  const _Segment(this.freqHz, this.durationMs);

  final double freqHz;
  final int durationMs;
}
