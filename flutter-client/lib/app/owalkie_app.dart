import 'package:flutter/material.dart';

import '../features/home/home_screen.dart';
import '../l10n/app_strings.dart';

class OwalkieApp extends StatelessWidget {
  const OwalkieApp({super.key});

  @override
  Widget build(BuildContext context) {
    final colorScheme = ColorScheme.fromSeed(
      seedColor: const Color(0xFF2E7D32),
      brightness: Brightness.dark,
    );

    return MaterialApp(
      title: AppStrings.appName,
      theme: ThemeData(
        colorScheme: colorScheme,
        useMaterial3: true,
        visualDensity: VisualDensity.standard,
        materialTapTargetSize: MaterialTapTargetSize.padded,
        // WCAG-friendly defaults for dark green seed theme.
        inputDecorationTheme: InputDecorationTheme(
          border: const OutlineInputBorder(),
          focusedBorder: OutlineInputBorder(
            borderSide: BorderSide(color: colorScheme.primary, width: 2),
          ),
        ),
      ),
      home: const HomeScreen(),
    );
  }
}
