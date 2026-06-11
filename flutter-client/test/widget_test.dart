import 'package:flutter_test/flutter_test.dart';

import 'package:owalkie_app/l10n/app_strings.dart';
import 'test_app_scope.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('home screen shows app title', (WidgetTester tester) async {
    await tester.pumpWidget(await buildTestApp());
    await tester.pumpAndSettle();

    expect(find.text(AppStrings.appName), findsOneWidget);
    expect(find.text(AppStrings.pttHold), findsOneWidget);
  });
}
