import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../features/home/home_screen.dart';
import '../features/settings/settings_screen.dart';
import '../features/signals/pattern_editor_screen.dart';

final appRouter = GoRouter(
  routes: [
    GoRoute(
      path: '/',
      builder: (context, state) => const HomeScreen(),
    ),
    GoRoute(
      path: '/settings',
      builder: (context, state) => const SettingsScreen(),
    ),
    GoRoute(
      path: '/signals/:kind/edit',
      builder: (context, state) {
        final kindName = state.pathParameters['kind'] ?? 'roger';
        final kind = kindName == 'calling'
            ? SignalEditorKind.calling
            : SignalEditorKind.roger;
        return PatternEditorScreen(
          kind: kind,
          editId: state.uri.queryParameters['id'],
        );
      },
    ),
  ],
);
