import 'dart:typed_data';

/// Decoded mono 16-bit PCM from a WAV file.
class WavPcm {
  const WavPcm(this.samples, this.sampleRate);

  final List<int> samples;
  final int sampleRate;

  static const defaultSampleRate = 44100;

  static WavPcm decode(Uint8List bytes) {
    if (bytes.length < 44) {
      return const WavPcm([], defaultSampleRate);
    }
    if (String.fromCharCodes(bytes.sublist(0, 4)) != 'RIFF' ||
        String.fromCharCodes(bytes.sublist(8, 12)) != 'WAVE') {
      return const WavPcm([], defaultSampleRate);
    }

    var offset = 12;
    var srcRate = defaultSampleRate;
    var dataStart = -1;
    var dataSize = 0;

    while (offset + 8 <= bytes.length) {
      final chunkId = String.fromCharCodes(bytes.sublist(offset, offset + 4));
      final chunkSize = ByteData.sublistView(bytes, offset + 4, offset + 8)
          .getInt32(0, Endian.little);
      final payloadStart = offset + 8;
      if (chunkId == 'fmt ' &&
          chunkSize >= 16 &&
          payloadStart + 16 <= bytes.length) {
        srcRate = ByteData.sublistView(bytes, payloadStart + 4, payloadStart + 8)
            .getInt32(0, Endian.little);
      }
      if (chunkId == 'data') {
        dataStart = payloadStart;
        dataSize = chunkSize;
        break;
      }
      offset = payloadStart + chunkSize + (chunkSize & 1);
    }

    if (dataStart < 0 ||
        dataSize <= 0 ||
        dataStart + dataSize > bytes.length) {
      return const WavPcm([], defaultSampleRate);
    }

    final sampleCount = dataSize ~/ 2;
    final raw = List<int>.filled(sampleCount, 0);
    var p = dataStart;
    for (var i = 0; i < sampleCount; i++) {
      raw[i] = ByteData.sublistView(bytes, p, p + 2).getInt16(0, Endian.little);
      p += 2;
    }
    final safeRate =
        (srcRate >= 4000 && srcRate <= 192000) ? srcRate : defaultSampleRate;
    return WavPcm(raw, safeRate);
  }

  /// Linear resample to [targetRate] (Kotlin [WalkieService.resampleLinear] parity).
  static List<int> resampleLinear(
    List<int> input,
    int sourceRate,
    int targetRate,
  ) {
    if (input.isEmpty) {
      return const [];
    }
    if (sourceRate <= 0 || targetRate <= 0 || sourceRate == targetRate) {
      return List<int>.from(input);
    }
    final outLen = (input.length * targetRate / sourceRate).round().clamp(1, 1 << 24);
    final out = List<int>.filled(outLen, 0);
    for (var i = 0; i < outLen; i++) {
      final srcPos = i * sourceRate / targetRate;
      final idx = srcPos.floor().clamp(0, input.length - 1);
      final frac = srcPos - idx;
      final next = (idx + 1).clamp(0, input.length - 1);
      final v = input[idx] * (1 - frac) + input[next] * frac;
      out[i] = v.round().clamp(-32768, 32767);
    }
    return out;
  }

  static List<int> applyGainPercent(List<int> pcm, int percent) {
    if (pcm.isEmpty) {
      return const [];
    }
    if (percent == 100) {
      return List<int>.from(pcm);
    }
    final gain = percent / 100.0;
    return [
      for (final s in pcm)
        (s * gain).round().clamp(-32768, 32767),
    ];
  }
}
