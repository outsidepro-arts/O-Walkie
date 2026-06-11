import 'dart:math' as math;

import 'package:owalkie_core/owalkie_core.dart';

/// Local UI tones (Kotlin [UiSignalPlayer]); does not use relay RX path.
abstract final class UiSignalPlayer {
  static const _sampleRate = 44100;

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

  static void _play(
    SessionService? session,
    List<_Segment> segments, {
    required double gain,
  }) {
    if (session == null || !session.isRunning) {
      return;
    }
    final pcm = _synthesize(segments, gain: gain);
    session.playLocalSamples(pcm, sampleRate: _sampleRate);
  }

  static List<int> _synthesize(List<_Segment> segments, {required double gain}) {
    final total = segments.fold<int>(
      0,
      (sum, s) => sum + (_sampleRate * s.durationMs) ~/ 1000,
    );
    final out = List<int>.filled(total.clamp(1, total), 0);
    var idx = 0;
    var phase = 0.0;
    for (final seg in segments) {
      final n = ((_sampleRate * seg.durationMs) / 1000).clamp(1, 1 << 20);
      final step = seg.freqHz > 0 ? 2 * math.pi * seg.freqHz / _sampleRate : 0.0;
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
