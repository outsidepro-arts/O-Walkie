import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:owalkie_app/l10n/a11y_strings.dart';
import 'package:owalkie_app/l10n/app_strings.dart';
import 'test_app_scope.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('home screen exposes key semantics for screen readers',
      (WidgetTester tester) async {
    final handle = tester.ensureSemantics();

    await tester.pumpWidget(await buildTestApp());
    await tester.pumpAndSettle();

    expect(
      tester.getSemantics(find.text(AppStrings.appName)),
      matchesSemantics(isHeader: true),
    );

    final ptt = tester.getSemantics(
      find.bySemanticsLabel(A11yStrings.pttUnavailable),
    );
    expect(ptt.label, A11yStrings.pttUnavailable);
    expect(ptt.hasFlag(SemanticsFlag.isButton), isTrue);
    expect(ptt.hasFlag(SemanticsFlag.hasEnabledState), isTrue);
    expect(ptt.hasFlag(SemanticsFlag.isEnabled), isFalse);

    final connection = tester.getSemantics(
      find.bySemanticsLabel(AppStrings.connectionStateDisconnected),
    );
    expect(connection.hasFlag(SemanticsFlag.isLiveRegion), isFalse);

    final more = tester.getSemantics(find.bySemanticsLabel(AppStrings.menuMore));
    expect(more.label, AppStrings.menuMore);

    handle.dispose();
  });

  testWidgets('More button receives keyboard focus via tab traversal',
      (WidgetTester tester) async {
    await tester.pumpWidget(await buildTestApp());
    await tester.pumpAndSettle();

    final moreButton =
        find.widgetWithText(OutlinedButton, AppStrings.menuMore);
    expect(moreButton, findsOneWidget);

    var moreFocused = false;
    for (var i = 0; i < 80; i++) {
      await tester.sendKeyEvent(LogicalKeyboardKey.tab);
      await tester.pump();
      final primary = FocusManager.instance.primaryFocus;
      if (primary?.context != null &&
          find
              .ancestor(
                of: find.byWidget(primary!.context!.widget),
                matching: moreButton,
              )
              .evaluate()
              .isNotEmpty) {
        moreFocused = true;
        break;
      }
    }

    expect(moreFocused, isTrue);
  });

  testWidgets('settings screen About section is reachable with semantics',
      (WidgetTester tester) async {
    final handle = tester.ensureSemantics();

    await tester.pumpWidget(await buildTestApp());
    await tester.pumpAndSettle();

    await tester.tap(find.widgetWithText(OutlinedButton, AppStrings.menuMore));
    await tester.pumpAndSettle();
    await tester.tap(find.text(AppStrings.menuSettings).last);
    await tester.pumpAndSettle();

    expect(find.text(AppStrings.settingsTitle), findsOneWidget);

    await tester.scrollUntilVisible(
      find.text(AppStrings.settingsAbout),
      200,
    );
    await tester.pumpAndSettle();

    expect(find.text(AppStrings.settingsAbout), findsOneWidget);
    expect(find.text(AppStrings.settingsAppVersion), findsOneWidget);
    expect(find.text(AppStrings.settingsProtocolVersion), findsOneWidget);
    expect(find.text(AppStrings.settingsGitHub), findsOneWidget);

    handle.dispose();
  });
}
