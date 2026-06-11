import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:owalkie_app/features/home/ptt_gesture_button.dart';

Future<void> _tabUntilPttFocused(WidgetTester tester) async {
  final ptt = find.byType(PttGestureButton);
  for (var i = 0; i < 40; i++) {
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.pump();
    final primary = FocusManager.instance.primaryFocus;
    if (primary?.context != null &&
        find
            .ancestor(
              of: find.byWidget(primary!.context!.widget),
              matching: ptt,
            )
            .evaluate()
            .isNotEmpty) {
      return;
    }
  }
  fail('PTT button did not receive keyboard focus');
}

Widget _harness({
  required bool enabled,
  required bool active,
  required VoidCallback onPttDown,
  required VoidCallback onPttUp,
  ValueChanged<bool>? onLatchedChanged,
}) {
  return MaterialApp(
    home: Scaffold(
      body: FocusTraversalGroup(
        child: Center(
          child: PttGestureButton(
            enabled: enabled,
            active: active,
            locked: false,
            pttLockSec: 0,
            sessionConnected: true,
            onPttDown: onPttDown,
            onPttUp: onPttUp,
            onLatchedChanged: onLatchedChanged ?? (_) {},
            child: const SizedBox(width: 120, height: 120),
          ),
        ),
      ),
    ),
  );
}

void main() {
  testWidgets('PTT receives tab focus and activates with Space', (tester) async {
    var downCount = 0;
    var latched = false;

    await tester.pumpWidget(
      _harness(
        enabled: true,
        active: false,
        onPttDown: () => downCount++,
        onPttUp: () {},
        onLatchedChanged: (value) => latched = value,
      ),
    );
    await tester.pumpAndSettle();

    await _tabUntilPttFocused(tester);

    await tester.sendKeyEvent(LogicalKeyboardKey.space);
    await tester.pump();
    expect(downCount, 1);
    expect(latched, isTrue);
  });

  testWidgets('PTT Space releases when active', (tester) async {
    var upCount = 0;

    await tester.pumpWidget(
      _harness(
        enabled: true,
        active: true,
        onPttDown: () {},
        onPttUp: () => upCount++,
      ),
    );
    await tester.pumpAndSettle();

    await _tabUntilPttFocused(tester);

    await tester.sendKeyEvent(LogicalKeyboardKey.space);
    await tester.pump();
    expect(upCount, 1);
  });

  testWidgets('PTT toggles with Enter when focused', (tester) async {
    var downCount = 0;

    await tester.pumpWidget(
      _harness(
        enabled: true,
        active: false,
        onPttDown: () => downCount++,
        onPttUp: () {},
      ),
    );
    await tester.pumpAndSettle();

    await _tabUntilPttFocused(tester);

    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pump();
    expect(downCount, 1);
  });

  testWidgets('swipe up latches and finger up keeps transmit', (tester) async {
    var downCount = 0;
    var upCount = 0;
    var latched = false;

    await tester.pumpWidget(
      StatefulBuilder(
        builder: (context, setState) {
          return MaterialApp(
            home: Scaffold(
              body: Center(
                child: PttGestureButton(
                  enabled: true,
                  active: downCount > 0,
                  locked: false,
                  pttLockSec: 0,
                  sessionConnected: true,
                  onPttDown: () => setState(() => downCount++),
                  onPttUp: () => upCount++,
                  onLatchedChanged: (value) => latched = value,
                  child: const SizedBox(width: 120, height: 120),
                ),
              ),
            ),
          );
        },
      ),
    );
    await tester.pumpAndSettle();

    final center = tester.getCenter(find.byType(PttGestureButton));
    final gesture = await tester.startGesture(center);
    await tester.pump();
    expect(downCount, 1);

    await gesture.moveBy(const Offset(0, -60));
    await tester.pump();
    expect(latched, isTrue);

    await gesture.up();
    await tester.pump();
    expect(upCount, 0);
  });

  testWidgets('tap releases latched transmit', (tester) async {
    var upCount = 0;
    var latched = false;
    var active = false;

    await tester.pumpWidget(
      StatefulBuilder(
        builder: (context, setState) {
          return MaterialApp(
            home: Scaffold(
              body: Center(
                child: PttGestureButton(
                  enabled: true,
                  active: active,
                  locked: false,
                  pttLockSec: 0,
                  sessionConnected: true,
                  onPttDown: () => setState(() => active = true),
                  onPttUp: () {
                    upCount++;
                    setState(() => active = false);
                  },
                  onLatchedChanged: (value) => latched = value,
                  child: const SizedBox(width: 120, height: 120),
                ),
              ),
            ),
          );
        },
      ),
    );
    await tester.pumpAndSettle();

    final center = tester.getCenter(find.byType(PttGestureButton));
    final hold = await tester.startGesture(center);
    await tester.pump();
    await hold.moveBy(const Offset(0, -60));
    await tester.pump();
    expect(latched, isTrue);
    await hold.up();
    await tester.pump();
    expect(upCount, 0);

    final tap = await tester.startGesture(center);
    await tester.pump();
    await tap.up();
    await tester.pump();

    expect(upCount, 1);
    expect(latched, isFalse);
  });
}
