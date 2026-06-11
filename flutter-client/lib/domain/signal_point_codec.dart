import 'signal_pattern.dart';

List<({double freqHz, int durationMs})> encodeSignalPoints(List<SignalPoint> points) {
  return [
    for (final p in points) (freqHz: p.freqHz, durationMs: p.durationMs),
  ];
}
