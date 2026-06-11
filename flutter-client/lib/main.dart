import 'dart:io' show Platform;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:window_manager/window_manager.dart';

import 'app/owalkie_app.dart';
import 'data/shared_preferences_provider.dart';
import 'l10n/app_strings.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  if (Platform.isWindows && Platform.environment['FLUTTER_TEST'] != 'true') {
    await windowManager.ensureInitialized();
    const windowOptions = WindowOptions(
      size: Size(480, 840),
      minimumSize: Size(400, 600),
      center: true,
      title: AppStrings.appName,
    );
    windowManager.waitUntilReadyToShow(windowOptions, () async {
      await windowManager.show();
      await windowManager.focus();
    });
  }

  final prefs = await SharedPreferences.getInstance();
  runApp(
    ProviderScope(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(prefs),
      ],
      child: const OwalkieApp(),
    ),
  );
}
