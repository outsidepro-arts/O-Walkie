import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart' hide MenuItem;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:tray_manager/tray_manager.dart';
import 'package:window_manager/window_manager.dart';

import '../../data/windows_settings_store.dart';
import '../../domain/windows_ptt_binding.dart';
import '../../features/home/home_screen_controller.dart';
import '../../l10n/app_strings.dart';
import 'windows_global_ptt.dart';

final desktopShellProvider = Provider<DesktopShell>((ref) {
  final shell = DesktopShell(ref);
  ref.onDispose(shell.dispose);
  return shell;
});

/// System tray, window close behavior, and global PTT hook on Windows.
class DesktopShell with TrayListener, WindowListener {
  DesktopShell(this._ref);

  final Ref _ref;
  bool _initialized = false;
  bool _exiting = false;
  bool get isActive => Platform.isWindows && !kIsWeb;

  bool get _sessionKeepAlive =>
      _ref.read(homeScreenControllerProvider.notifier).sessionKeepAlive;

  Future<void> init() async {
    if (!isActive || _initialized) {
      return;
    }
    _initialized = true;

    trayManager.addListener(this);
    windowManager.addListener(this);
    await windowManager.setPreventClose(true);

    await trayManager.setIcon('assets/tray_icon.ico');
    await trayManager.setToolTip(AppStrings.appName);
    await trayManager.setContextMenu(
      Menu(
        items: [
          MenuItem(key: 'show', label: AppStrings.trayMenuShow),
          MenuItem.separator(),
          MenuItem(key: 'exit', label: AppStrings.trayMenuExit),
        ],
      ),
    );

  }

  Future<void> applyStoredBinding() async {
    final binding = _ref.read(windowsSettingsStoreProvider).loadBinding();
    await setGlobalPttBinding(binding, persist: false);
  }

  Future<void> setGlobalPttBinding(
    WindowsPttBinding? binding, {
    bool persist = true,
  }) async {
    if (!isActive) {
      return;
    }
    if (binding == null || !binding.assigned) {
      await WindowsGlobalPtt.clearBinding();
      await WindowsGlobalPtt.uninstallHook();
    } else {
      await WindowsGlobalPtt.setBinding(binding);
      await WindowsGlobalPtt.installHook();
    }
    if (persist) {
      await _ref.read(windowsSettingsStoreProvider).saveBinding(binding);
    }
  }

  Future<void> setMinimizeToTrayOnClose(bool enabled) async {
    await _ref.read(windowsSettingsStoreProvider).setMinimizeToTrayOnClose(enabled);
  }

  Future<void> hideToTray() async {
    await windowManager.hide();
  }

  /// Full quit when the relay session is idle (window close / system back).
  Future<void> exitApplication() async {
    await _exitApp();
  }

  Future<void> showMainWindow() async {
    await windowManager.show();
    await windowManager.focus();
  }

  Future<void> _exitApp() async {
    if (_exiting) {
      return;
    }
    _exiting = true;
    await _ref.read(homeScreenControllerProvider.notifier).prepareForAppExit();
    await WindowsGlobalPtt.uninstallHook();
    trayManager.removeListener(this);
    windowManager.removeListener(this);
    await windowManager.setPreventClose(false);
    await trayManager.destroy();
    await windowManager.destroy();
  }

  Future<void> _exitAppForced() async {
    if (_exiting) {
      return;
    }
    _exiting = true;
    await _ref.read(homeScreenControllerProvider.notifier).shutdownForAppExit();
    await WindowsGlobalPtt.uninstallHook();
    trayManager.removeListener(this);
    windowManager.removeListener(this);
    await windowManager.setPreventClose(false);
    await trayManager.destroy();
    await windowManager.destroy();
  }

  void dispose() {
    if (!isActive || !_initialized) {
      return;
    }
    trayManager.removeListener(this);
    windowManager.removeListener(this);
  }

  @override
  void onTrayIconMouseDown() {
    unawaited(showMainWindow());
  }

  @override
  void onTrayIconRightMouseDown() {
    unawaited(trayManager.popUpContextMenu());
  }

  @override
  void onTrayMenuItemClick(MenuItem menuItem) {
    switch (menuItem.key) {
      case 'show':
        unawaited(showMainWindow());
      case 'exit':
        unawaited(_exitAppForced());
    }
  }

  @override
  void onWindowClose() {
    if (_exiting) {
      return;
    }
    if (_sessionKeepAlive) {
      unawaited(hideToTray());
      return;
    }
    unawaited(_exitApp());
  }
}
