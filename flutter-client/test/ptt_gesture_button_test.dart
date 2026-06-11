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

void main() {
  testWidgets('PTT receives tab focus and activates with Space', (tester) async {
    var downCount = 0;

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: FocusTraversalGroup(
            child: PttGestureButton(
              enabled: true,
              active: false,
              locked: false,
              pttLockSec: 0,
              sessionConnected: true,
              onPttDown: () => downCount++,
              onPttUp: () {},
              child: const Text('Hold to talk'),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    await _tabUntilPttFocused(tester);

    await tester.sendKeyEvent(LogicalKeyboardKey.space);
    await tester.pump();
    expect(downCount, 1);
  });

  testWidgets('PTT Space releases when active', (tester) async {
    var upCount = 0;

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: FocusTraversalGroup(
            child: PttGestureButton(
              enabled: true,
              active: true,
              locked: false,
              pttLockSec: 0,
              sessionConnected: true,
              onPttDown: () {},
              onPttUp: () => upCount++,
              child: const Text('Hold to talk'),
            ),
          ),
        ),
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
      MaterialApp(
        home: Scaffold(
          body: FocusTraversalGroup(
            child: PttGestureButton(
              enabled: true,
              active: false,
              locked: false,
              pttLockSec: 0,
              sessionConnected: true,
              onPttDown: () => downCount++,
              onPttUp: () {},
              child: const Text('Hold to talk'),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    await _tabUntilPttFocused(tester);

    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pump();
    expect(downCount, 1);
  });
}
