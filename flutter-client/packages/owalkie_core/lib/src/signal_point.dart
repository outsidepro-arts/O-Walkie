/// Tone segment for roger/call PCM generation (worker isolate).
class SignalPoint {
  const SignalPoint({required this.freqHz, required this.durationMs});

  final double freqHz;
  final int durationMs;
}
