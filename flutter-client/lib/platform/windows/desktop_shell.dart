import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart' hide MenuItem;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:hotkey_manager/hotkey_manager.dart';
import 'package:tray_manager/tray_manager.dart';
import 'package:window_manager/window_manager.dart';

import '../../data/windows_settings_store.dart';
import '../../features/home/home_screen_controller.dart';
import '../../l10n/app_strings.dart';

final desktopShellProvider = Provider<DesktopShell>((ref) {
  final shell = DesktopShell(ref);
  ref.onDispose(shell.dispose);
  return shell;
});

/// System tray, window close behavior, and global PTT hotkey on Windows.
class DesktopShell with TrayListener, WindowListener {
  DesktopShell(this._ref);

  final Ref _ref;
  bool _initialized = false;
  bool _exiting = false;
  bool _minimizeToTray = false;
  HotKey? _registeredHotKey;
  VoidCallback? _globalPttHandler;

  bool get isActive => Platform.isWindows && !kIsWeb;

  Future<void> init() async {
    if (!isActive || _initialized) {
      return;
    }
    _initialized = true;
    _globalPttHandler = () {
      _ref.read(homeScreenControllerProvider.notifier).togglePttLatch();
    };

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

    await applyStoredHotKey();
  }

  Future<void> applyStoredHotKey() async {
    final hotKey = _ref.read(windowsSettingsStoreProvider).loadHotKey();
    await setGlobalPttHotKey(hotKey, persist: false);
  }

  Future<void> setGlobalPttHotKey(HotKey? hotKey, {bool persist = true}) async {
    if (!isActive) {
      return;
    }
    if (_registeredHotKey != null) {
      await hotKeyManager.unregister(_registeredHotKey!);
      _registeredHotKey = null;
    }
    if (hotKey != null) {
      final scoped = HotKey(
        identifier: hotKey.identifier,
        key: hotKey.key,
        modifiers: hotKey.modifiers,
        scope: HotKeyScope.system,
      );
      await hotKeyManager.register(
        scoped,
        keyDownHandler: (_) {
          scheduleMicrotask(() => _globalPttHandler?.call());
        },
      );
      _registeredHotKey = scoped;
    }
    if (persist) {
      await _ref.read(windowsSettingsStoreProvider).saveHotKey(hotKey);
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
    if (_registeredHotKey != null) {
      await hotKeyManager.unregister(_registeredHotKey!);
      _registeredHotKey = null;
    }
    await hotKeyManager.unregisterAll();
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
