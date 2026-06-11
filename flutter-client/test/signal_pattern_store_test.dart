import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:owalkie_app/data/signal_pattern_store.dart';
import 'package:owalkie_app/domain/signal_pattern.dart';

void main() {
  test('roger store saves and selects custom pattern', () async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();
    final store = RogerPatternStore(prefs);
    final saved = await store.saveCustomPattern(
      'Team beep',
      const [SignalPoint(freqHz: 1000, durationMs: 50)],
    );
    expect(store.getSelectedPattern().id, saved.id);
    expect(store.getAllPatterns().any((p) => p.name == 'Team beep'), isTrue);
  });

  test('calling store expands repeat count in pattern', () async {
    const pattern = BuiltInCallPatterns.variant1;
    expect(pattern.expandedPoints().length, pattern.points.length * 9);
  });
}
