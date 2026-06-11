import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart' hide MenuItem;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:tray_manager/tray_manager.dart';
import 'package:window_manager/window_manager.dart';

import '../../data/windows_settings_store.dart';
import '../../domain/windows_ptt_binding.dart';
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
  bool _minimizeToTray = false;
  bool get isActive => Platform.isWindows && !kIsWeb;

  Future<void> init() async {
    if (!isActive || _initialized) {
      return;
    }
    _initialized = true;

    final store = _ref.read(windowsSettingsStoreProvider);
    _minimizeToTray = store.minimizeToTrayOnClose();

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
    _minimizeToTray = enabled;
    await _ref.read(windowsSettingsStoreProvider).setMinimizeToTrayOnClose(enabled);
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
        unawaited(_exitApp());
    }
  }

  @override
  void onWindowClose() {
    if (_exiting) {
      return;
    }
    if (_minimizeToTray) {
      unawaited(windowManager.hide());
      return;
    }
    unawaited(_exitApp());
  }
}
