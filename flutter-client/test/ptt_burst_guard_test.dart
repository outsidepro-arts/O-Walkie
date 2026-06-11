import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/domain/ptt_burst_guard.dart';

void main() {
  test('blocks press after rapid releases', () async {
    final guard = PttBurstGuard();
    expect(guard.onPressAttempt(), isTrue);
    guard.onRelease();
    guard.onRelease();
    guard.onRelease();
    expect(guard.pressBlocked, isTrue);
    expect(guard.onPressAttempt(), isFalse);
  });

  test('resets after decay window', () async {
    final guard = PttBurstGuard();
    guard.onRelease();
    guard.onRelease();
    guard.onRelease();
    expect(guard.pressBlocked, isTrue);
    await Future<void>.delayed(
      const Duration(milliseconds: PttBurstGuard.releaseBurstTimerMs + 50),
    );
    expect(guard.pressBlocked, isFalse);
    expect(guard.onPressAttempt(), isTrue);
  });

  test('reset clears blocked state immediately', () {
    final guard = PttBurstGuard();
    guard.onRelease();
    guard.onRelease();
    guard.onRelease();
    guard.reset();
    expect(guard.pressBlocked, isFalse);
    expect(guard.onPressAttempt(), isTrue);
  });
}
