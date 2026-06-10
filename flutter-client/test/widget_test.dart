import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:owalkie_app/app/owalkie_app.dart';
import 'package:owalkie_app/l10n/app_strings.dart';

void main() {
  testWidgets('home screen shows app title', (WidgetTester tester) async {
    await tester.pumpWidget(
      const ProviderScope(child: OwalkieApp()),
    );
    await tester.pumpAndSettle();

    expect(find.text(AppStrings.appName), findsOneWidget);
    expect(find.text(AppStrings.pttHold), findsOneWidget);
  });
}
