import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/features/home/ptt_touch_policy.dart';

void main() {
  group('pttPointerUpAction', () {
    test('short tap when idle toggles on', () {
      expect(
        pttPointerUpAction(holdMode: false, txActive: false),
        PttPointerUpAction.toggleOn,
      );
    });

    test('short tap when latched toggles off', () {
      expect(
        pttPointerUpAction(holdMode: false, txActive: true),
        PttPointerUpAction.toggleOff,
      );
    });

    test('hold release ends transmit', () {
      expect(
        pttPointerUpAction(holdMode: true, txActive: true),
        PttPointerUpAction.releaseHold,
      );
    });
  });
}
