import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/features/home/ptt_touch_policy.dart';

void main() {
  group('pttSwipeUpLockTriggered', () {
    test('locks after upward swipe exceeds threshold', () {
      expect(
        pttSwipeUpLockTriggered(startY: 120, currentY: 60),
        isTrue,
      );
    });

    test('does not lock on small movement', () {
      expect(
        pttSwipeUpLockTriggered(startY: 120, currentY: 90),
        isFalse,
      );
    });
  });

  group('pttPointerUpAction', () {
    test('tap to release when latched', () {
      expect(
        pttPointerUpAction(latched: true, tapToReleasePending: true),
        PttPointerUpAction.releaseLatched,
      );
    });

    test('hold release ends transmit when not latched', () {
      expect(
        pttPointerUpAction(latched: false, tapToReleasePending: false),
        PttPointerUpAction.releaseTransmit,
      );
    });

    test('finger up keeps transmit when latched', () {
      expect(
        pttPointerUpAction(latched: true, tapToReleasePending: false),
        PttPointerUpAction.none,
      );
    });
  });
}
