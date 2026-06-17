import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/domain/signal_pattern.dart';
import 'package:owalkie_app/domain/signal_sequence_clipboard.dart';

void main() {
  group('signalSequenceToJson / signalSequenceParseFromText', () {
    const rogerPoints = [
      SignalPoint(freqHz: 890, durationMs: 20),
      SignalPoint(freqHz: 670, durationMs: 20),
      SignalPoint(freqHz: 890, durationMs: 45),
      SignalPoint(freqHz: 1000, durationMs: 28),
    ];

    test('round-trip roger pattern (no repetitions)', () {
      final json = signalSequenceToJson(
        name: 'My Roger',
        points: rogerPoints,
        includeRepetitions: false,
        repetitions: 1,
      );
      expect(json, contains('oWalkieSignalSequence'));

      final payload = signalSequenceParseFromText(json);
      expect(payload, isNotNull);
      expect(payload!.name, 'My Roger');
      expect(payload.repeatCount, 1);
      expect(payload.points.length, 4);
      expect(payload.points[0].freqHz, 890);
      expect(payload.points[0].durationMs, 20);
      expect(payload.points[3].freqHz, 1000);
      expect(payload.points[3].durationMs, 28);
    });

    test('round-trip calling pattern (with repetitions)', () {
      const callPoints = [
        SignalPoint(freqHz: 2300, durationMs: 70),
        SignalPoint(freqHz: 1850, durationMs: 70),
        SignalPoint(freqHz: 1450, durationMs: 70),
      ];
      final json = signalSequenceToJson(
        name: 'My Call',
        points: callPoints,
        includeRepetitions: true,
        repetitions: 9,
      );
      expect(json, contains('"repetitions":9'));

      final payload = signalSequenceParseFromText(json);
      expect(payload, isNotNull);
      expect(payload!.name, 'My Call');
      expect(payload.repeatCount, 9);
      expect(payload.points.length, 3);
    });

    test('round-trip calling pattern clamps repetitions', () {
      const point = [SignalPoint(freqHz: 1000, durationMs: 50)];
      final json = signalSequenceToJson(
        name: 'Clamp Test',
        points: point,
        includeRepetitions: true,
        repetitions: 9999,
      );
      expect(json, contains('"repetitions":500'));

      final payload = signalSequenceParseFromText(json);
      expect(payload, isNotNull);
      expect(payload!.repeatCount, 500);
    });

    test('produces Android-compatible JSON structure', () {
      final json = signalSequenceToJson(
        name: 'Test',
        points: [const SignalPoint(freqHz: 1000, durationMs: 50)],
        includeRepetitions: true,
        repetitions: 4,
      );
      final decoded = jsonDecode(json) as Map<String, dynamic>;

      // Android format: {"oWalkieSignalSequence": {"version": 1, "signal": {...}, "points": [...], "repetitions": N}}
      final envelope = decoded['oWalkieSignalSequence'] as Map<String, dynamic>;
      expect(envelope['version'], 1);
      expect((envelope['signal'] as Map<String, dynamic>)['name'], 'Test');
      expect((envelope['points'] as List<dynamic>).length, 1);
      expect(envelope['repetitions'], 4);

      // Field order matches Android: durationMs first, then freqHz
      final point = (envelope['points'] as List<dynamic>).first as Map<String, dynamic>;
      expect(point.keys, containsAll(['durationMs', 'freqHz']));
      expect(point['durationMs'], 50);
      expect(point['freqHz'], 1000);
    });

    test('decodes Android-generated JSON string', () {
      // Simulate what Android SignalSequenceClipboard.toJson produces
      final androidJson = jsonEncode({
        'oWalkieSignalSequence': {
          'version': 1,
          'signal': {'name': 'From Android'},
          'points': [
            {'durationMs': 20, 'freqHz': 890},
            {'durationMs': 20, 'freqHz': 670},
          ],
          'repetitions': 3,
        },
      });
      final payload = signalSequenceParseFromText(androidJson);
      expect(payload, isNotNull);
      expect(payload!.name, 'From Android');
      expect(payload.repeatCount, 3);
      expect(payload.points.length, 2);
    });

    test('decodes legacy flat envelope format', () {
      final legacyJson = jsonEncode({
        'oWalkieSignalSequence': 1,
        'name': 'Legacy',
        'points': [
          {'durationMs': 100, 'freqHz': 440},
        ],
        'repeatCount': 5,
      });
      final payload = signalSequenceParseFromText(legacyJson);
      expect(payload, isNotNull);
      expect(payload!.name, 'Legacy');
      expect(payload.repeatCount, 5);
      expect(payload.points.length, 1);
    });

    test('legacy format falls back to repetitions field', () {
      final legacyJson = jsonEncode({
        'oWalkieSignalSequence': 1,
        'name': 'Legacy Rep',
        'points': [
          {'durationMs': 100, 'freqHz': 440},
        ],
        'repetitions': 7,
      });
      final payload = signalSequenceParseFromText(legacyJson);
      expect(payload, isNotNull);
      expect(payload!.repeatCount, 7);
    });

    test('extracts JSON object from text with surrounding garbage', () {
      final json = signalSequenceToJson(
        name: 'Embedded',
        points: [const SignalPoint(freqHz: 600, durationMs: 30)],
        includeRepetitions: false,
        repetitions: 1,
      );
      final textWithGarbage = 'Some text before $json and some after';
      final payload = signalSequenceParseFromText(textWithGarbage);
      expect(payload, isNotNull);
      expect(payload!.name, 'Embedded');
    });

    test('pause point (freqHz = 0) is valid', () {
      final json = signalSequenceToJson(
        name: 'Pause Test',
        points: [const SignalPoint(freqHz: 0, durationMs: 100)],
        includeRepetitions: false,
        repetitions: 1,
      );
      final payload = signalSequenceParseFromText(json);
      expect(payload, isNotNull);
      expect(payload!.points.length, 1);
      expect(payload.points[0].freqHz, 0);
      expect(payload.points[0].durationMs, 100);
    });

    test('returns null for empty text', () {
      expect(signalSequenceParseFromText(''), isNull);
    });

    test('returns null for non-JSON text', () {
      expect(signalSequenceParseFromText('not json at all'), isNull);
    });

    test('returns null for missing magic key', () {
      final bad = jsonEncode({'someOtherKey': {'version': 1}});
      expect(signalSequenceParseFromText(bad), isNull);
    });

    test('returns null for wrong version', () {
      final bad = jsonEncode({
        'oWalkieSignalSequence': {
          'version': 2,
          'signal': {'name': 'x'},
          'points': [{'durationMs': 50, 'freqHz': 1000}],
        },
      });
      expect(signalSequenceParseFromText(bad), isNull);
    });

    test('returns null for empty points array', () {
      final bad = jsonEncode({
        'oWalkieSignalSequence': {
          'version': 1,
          'signal': {'name': 'x'},
          'points': [],
        },
      });
      expect(signalSequenceParseFromText(bad), isNull);
    });

    test('returns null for missing point fields', () {
      final bad = jsonEncode({
        'oWalkieSignalSequence': {
          'version': 1,
          'signal': {'name': 'x'},
          'points': [{'durationMs': 50}],
        },
      });
      expect(signalSequenceParseFromText(bad), isNull);
    });

    test('returns null for invalid duration (zero)', () {
      final bad = jsonEncode({
        'oWalkieSignalSequence': {
          'version': 1,
          'signal': {'name': 'x'},
          'points': [{'durationMs': 0, 'freqHz': 1000}],
        },
      });
      expect(signalSequenceParseFromText(bad), isNull);
    });

    test('returns null for invalid frequency (negative)', () {
      final bad = jsonEncode({
        'oWalkieSignalSequence': {
          'version': 1,
          'signal': {'name': 'x'},
          'points': [{'durationMs': 50, 'freqHz': -1}],
        },
      });
      expect(signalSequenceParseFromText(bad), isNull);
    });
  });
}
