import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../data/orientation_store.dart';
import '../l10n/app_strings.dart';
import 'app_router.dart';

class OwalkieApp extends ConsumerStatefulWidget {
  const OwalkieApp({super.key});

  @override
  ConsumerState<OwalkieApp> createState() => _OwalkieAppState();
}

class _OwalkieAppState extends ConsumerState<OwalkieApp> {
  @override
  void initState() {
    super.initState();
    _applyOrientation();
  }

  Future<void> _applyOrientation() async {
    final store = ref.read(orientationStoreProvider);
    await store.apply(await store.load());
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = ColorScheme.fromSeed(
      seedColor: const Color(0xFF2E7D32),
      brightness: Brightness.dark,
    );

    return MaterialApp.router(
      title: AppStrings.appName,
      theme: ThemeData(
        colorScheme: colorScheme,
        useMaterial3: true,
        visualDensity: VisualDensity.standard,
        materialTapTargetSize: MaterialTapTargetSize.padded,
        inputDecorationTheme: InputDecorationTheme(
          border: const OutlineInputBorder(),
          focusedBorder: OutlineInputBorder(
            borderSide: BorderSide(color: colorScheme.primary, width: 2),
          ),
        ),
      ),
      routerConfig: appRouter,
    );
  }
}
