import 'package:flutter/semantics.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:owalkie_app/app/owalkie_app.dart';
import 'package:owalkie_app/l10n/a11y_strings.dart';
import 'package:owalkie_app/l10n/app_strings.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues({});
  });

  testWidgets('home screen exposes key semantics for screen readers',
      (WidgetTester tester) async {
    final handle = tester.ensureSemantics();

    await tester.pumpWidget(
      const ProviderScope(child: OwalkieApp()),
    );
    await tester.pumpAndSettle();

    expect(
      tester.getSemantics(find.text(AppStrings.appName)),
      matchesSemantics(isHeader: true),
    );

    expect(
      tester.getSemantics(find.text(AppStrings.serverProfiles)),
      matchesSemantics(isHeader: true),
    );

    final ptt = tester.getSemantics(find.bySemanticsLabel(A11yStrings.pttLabel));
    expect(ptt.label, A11yStrings.pttLabel);
    expect(ptt.hint, A11yStrings.pttDisabledHint);
    expect(ptt.hasFlag(SemanticsFlag.isButton), isTrue);
    expect(ptt.hasFlag(SemanticsFlag.hasEnabledState), isTrue);
    expect(ptt.hasFlag(SemanticsFlag.isEnabled), isFalse);

    expect(find.text(AppStrings.menuMore), findsOneWidget);

    handle.dispose();
  });
}
