import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_core/owalkie_core.dart';

void main() {
  group('LinkSignal.byteToPercent', () {
    test('maps 0 and 255', () {
      expect(LinkSignal.byteToPercent(0), 0);
      expect(LinkSignal.byteToPercent(255), 100);
    });

    test('rounds mid-range byte', () {
      expect(LinkSignal.byteToPercent(128), 50);
    });
  });
}
