import 'dart:convert';

import 'signal_pattern.dart';

const _magicKey = 'oWalkieSignalSequence';
const _formatVersion = 1;

class SignalSequencePayload {
  const SignalSequencePayload({
    required this.name,
    required this.points,
    required this.repeatCount,
  });

  final String name;
  final List<SignalPoint> points;
  final int repeatCount;
}

String signalSequenceToJson({
  required String name,
  required List<SignalPoint> points,
  required bool includeRepetitions,
  required int repetitions,
}) {
  final envelope = <String, dynamic>{
    'version': _formatVersion,
    'signal': <String, dynamic>{'name': name},
    'points': [
      for (final p in points) <String, dynamic>{
        'durationMs': p.durationMs,
        'freqHz': p.freqHz,
      },
    ],
  };
  if (includeRepetitions) {
    envelope['repetitions'] = repetitions.clamp(1, 500);
  }
  return jsonEncode({_magicKey: envelope});
}

SignalSequencePayload? signalSequenceParseFromText(String fullText) {
  final trimmed = fullText.trim();
  final candidates = <String>{};
  if (trimmed.isNotEmpty) {
    candidates.add(trimmed);
  }
  var start = 0;
  while (start < fullText.length) {
    final brace = fullText.indexOf('{', start);
    if (brace < 0) break;
    final extracted = _extractBalancedJsonObject(fullText, brace);
    if (extracted != null) {
      candidates.add(extracted);
    }
    start = brace + 1;
  }
  for (final text in candidates) {
    final result = _parseNestedEnvelope(text);
    if (result != null) return result;
    final legacyResult = _parseLegacyFlatEnvelope(text);
    if (legacyResult != null) return legacyResult;
  }
  return null;
}

SignalSequencePayload? _parseNestedEnvelope(String jsonText) {
  try {
    final root = jsonDecode(jsonText) as Map<String, dynamic>;
    final envelope = root[_magicKey];
    if (envelope is! Map<String, dynamic>) return null;
    final ver = envelope['version'];
    if (ver is! num) return null;
    if (ver.toInt() != _formatVersion) return null;
    final signalObj = envelope['signal'];
    if (signalObj is! Map<String, dynamic>) return null;
    final name = signalObj['name'] as String? ?? '';
    final arr = envelope['points'];
    if (arr is! List) return null;
    final points = _parsePointsArray(arr);
    if (points == null) return null;
    final repeatCount = _parseRepetitionsField(envelope);
    return SignalSequencePayload(name: name, points: points, repeatCount: repeatCount);
  } catch (_) {
    return null;
  }
}

SignalSequencePayload? _parseLegacyFlatEnvelope(String jsonText) {
  try {
    final root = jsonDecode(jsonText) as Map<String, dynamic>;
    final magic = root[_magicKey];
    if (magic is! num) return null;
    final arr = root['points'];
    if (arr is! List) return null;
    final points = _parsePointsArray(arr);
    if (points == null || points.isEmpty) return null;
    final name = root['name'] as String? ?? '';
    final repeatCount = _parseLegacyRepeatCount(root);
    return SignalSequencePayload(name: name, points: points, repeatCount: repeatCount);
  } catch (_) {
    return null;
  }
}

List<SignalPoint>? _parsePointsArray(List arr) {
  final out = <SignalPoint>[];
  for (final item in arr) {
    if (item is! Map) return null;
    final freq = item['freqHz'];
    final dur = item['durationMs'];
    if (freq is! num || dur is! num) return null;
    if (freq < 0 || dur <= 0) return null;
    out.add(SignalPoint(freqHz: freq.toDouble(), durationMs: dur.toInt()));
  }
  return out.isEmpty ? null : out;
}

int _parseRepetitionsField(Map<String, dynamic> envelope) {
  if (!envelope.containsKey('repetitions')) return 1;
  final raw = envelope['repetitions'];
  return _parseIntish(raw)?.clamp(1, 500) ?? 1;
}

int _parseLegacyRepeatCount(Map<String, dynamic> root) {
  final raw = root['repeatCount'] ?? root['repetitions'] ?? 1;
  return _parseIntish(raw)?.clamp(1, 500) ?? 1;
}

int? _parseIntish(dynamic raw) {
  if (raw is num) return raw.toInt();
  if (raw is String) return int.tryParse(raw.trim());
  return null;
}

String? _extractBalancedJsonObject(String s, int openIndex) {
  if (openIndex >= s.length || s[openIndex] != '{') return null;
  var depth = 0;
  var inString = false;
  var escape = false;
  for (var i = openIndex; i < s.length; i++) {
    final c = s[i];
    if (inString) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        inString = false;
      }
    } else {
      if (c == '"') {
        inString = true;
      } else if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0) return s.substring(openIndex, i + 1);
      }
    }
  }
  return null;
}
