import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'desktop_shell.dart';

/// Initializes [DesktopShell] once on Windows desktop builds.
class DesktopBootstrap extends ConsumerStatefulWidget {
  const DesktopBootstrap({super.key, required this.child});

  final Widget child;

  @override
  ConsumerState<DesktopBootstrap> createState() => _DesktopBootstrapState();
}

class _DesktopBootstrapState extends ConsumerState<DesktopBootstrap> {
  @override
  void initState() {
    super.initState();
    if (Platform.isWindows &&
        !kIsWeb &&
        Platform.environment['FLUTTER_TEST'] != 'true') {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        unawaited(ref.read(desktopShellProvider).init());
      });
    }
  }

  @override
  Widget build(BuildContext context) => widget.child;
}
