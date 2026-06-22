import 'dart:async';

import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../../l10n/a11y_strings.dart';
import '../../l10n/app_strings.dart';
import '../../platform/native_platform.dart';

class PermissionsScreen extends StatefulWidget {
  const PermissionsScreen({super.key});

  @override
  State<PermissionsScreen> createState() => _PermissionsScreenState();
}

class _PermissionsScreenState extends State<PermissionsScreen> {
  final List<_PermissionItem> _permissions = [];
  bool _ready = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final items = <_PermissionItem>[
      _PermissionItem(
        name: AppStrings.settingsAudioInputDevice,
        description: AppStrings.permissionsMicDescription,
        type: _PermissionType.runtime,
        critical: true,
        check: NativePlatform.hasMicrophonePermission,
        request: NativePlatform.requestMicrophonePermission,
      ),
      _PermissionItem(
        name: AppStrings.settingsPauseDuringPhoneCall,
        description: AppStrings.permissionsPhoneStateDescription,
        type: _PermissionType.runtime,
        critical: true,
        check: NativePlatform.hasPhoneStatePermission,
        request: NativePlatform.requestPhoneStatePermission,
        platformGuard: () => NativePlatform.isAndroid,
      ),
      _PermissionItem(
        name: AppStrings.permissionsNotifications,
        description: AppStrings.permissionsNotificationDescription,
        type: _PermissionType.runtime,
        critical: true,
        check: NativePlatform.hasNotificationPermission,
        request: NativePlatform.requestNotificationPermission,
        platformGuard: () => NativePlatform.isAndroid,
      ),
      _PermissionItem(
        name: AppStrings.permissionsBluetooth,
        description: AppStrings.permissionsBluetoothDescription,
        type: _PermissionType.runtime,
        check: NativePlatform.hasBluetoothConnectPermission,
        request: NativePlatform.requestBluetoothConnectPermission,
        platformGuard: () => NativePlatform.isAndroid,
      ),
      _PermissionItem(
        name: AppStrings.permissionsInternet,
        description: AppStrings.permissionsInternetDescription,
        type: _PermissionType.installTime,
      ),
      _PermissionItem(
        name: AppStrings.permissionsNetworkState,
        description: AppStrings.permissionsNetworkStateDescription,
        type: _PermissionType.installTime,
      ),
      _PermissionItem(
        name: AppStrings.permissionsWifiState,
        description: AppStrings.permissionsWifiStateDescription,
        type: _PermissionType.installTime,
      ),
      _PermissionItem(
        name: AppStrings.permissionsAudioSettings,
        description: AppStrings.permissionsAudioSettingsDescription,
        type: _PermissionType.installTime,
      ),
      _PermissionItem(
        name: AppStrings.permissionsForegroundService,
        description: AppStrings.permissionsForegroundServiceDescription,
        type: _PermissionType.installTime,
      ),
    ];

    for (final item in items) {
      if (item.platformGuard != null && !item.platformGuard!()) {
        continue;
      }
      if (item.type == _PermissionType.runtime && item.check != null) {
        item.granted = await item.check!();
      }
    }

    if (!mounted) return;
    setState(() {
      _permissions.addAll(
        items.where((p) => p.platformGuard == null || p.platformGuard!()),
      );
      _ready = true;
    });
  }

  Future<void> _requestPermission(_PermissionItem item) async {
    if (item.request == null) return;
    final granted = await item.request!();
    if (!mounted) return;
    setState(() => item.granted = granted);
  }

  List<_PermissionItem> get _runtimePermissions =>
      _permissions.where((p) => p.type == _PermissionType.runtime).toList();

  List<_PermissionItem> get _installTimePermissions =>
      _permissions.where((p) => p.type == _PermissionType.installTime).toList();

  @override
  Widget build(BuildContext context) {
    if (!_ready) {
      return Scaffold(
        appBar: AppBar(
          title: Text(AppStrings.permissionsTitle),
          leading: IconButton(
            tooltip: A11yStrings.back,
            icon: const Icon(Icons.arrow_back),
            onPressed: () => context.pop(),
          ),
        ),
        body: const Center(child: CircularProgressIndicator()),
      );
    }

    final runtimeGranted =
        _runtimePermissions.where((p) => p.granted).length;
    final runtimeTotal = _runtimePermissions.length;

    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.permissionsTitle),
        leading: IconButton(
          tooltip: A11yStrings.back,
          icon: const Icon(Icons.arrow_back),
          onPressed: () => context.pop(),
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _buildSummaryHeader(runtimeGranted, runtimeTotal),
          const SizedBox(height: 16),
          _buildSection(
            AppStrings.permissionsSectionRuntime,
            _runtimePermissions,
          ),
          const SizedBox(height: 16),
          _buildSection(
            AppStrings.permissionsSectionInstallTime,
            _installTimePermissions,
          ),
        ],
      ),
    );
  }

  Widget _buildSummaryHeader(int granted, int total) {
    final allGranted = granted == total;
    return Semantics(
      container: true,
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Icon(
                allGranted ? Icons.check_circle : Icons.warning_amber,
                color: allGranted ? Colors.green : Colors.orange,
                size: 32,
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  allGranted
                      ? AppStrings.settingsPermissionsAllGranted
                      : AppStrings.settingsPermissionsGrantedOf(
                          granted, total),
                  style: Theme.of(context).textTheme.bodyLarge,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildSection(String title, List<_PermissionItem> items) {
    return Semantics(
      container: true,
      explicitChildNodes: true,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Semantics(
            header: true,
            label: title,
            excludeSemantics: true,
            child: Text(
              title,
              style: Theme.of(context).textTheme.titleSmall,
            ),
          ),
          const SizedBox(height: 8),
          for (final item in items) _buildPermissionTile(item),
        ],
      ),
    );
  }

  Widget _buildPermissionTile(_PermissionItem item) {
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: ListTile(
        leading: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              item.granted
                  ? Icons.check_circle_outline
                  : Icons.cancel_outlined,
              color: item.granted ? Colors.green : Colors.red,
            ),
            if (item.critical)
              const Padding(
                padding: EdgeInsets.only(left: 4),
                child: Icon(
                  Icons.priority_high,
                  color: Colors.orange,
                  size: 20,
                ),
              ),
          ],
        ),
        title: Text(item.name),
        subtitle: Text(
          item.description,
          style: Theme.of(context).textTheme.bodySmall,
        ),
        trailing: item.type == _PermissionType.runtime && !item.granted
            ? TextButton(
                onPressed: () => _requestPermission(item),
                child: Text(AppStrings.permissionsGrant),
              )
            : Text(
                item.type == _PermissionType.installTime
                    ? AppStrings.permissionsInstallTime
                    : item.granted
                        ? AppStrings.permissionsGranted
                        : AppStrings.permissionsNotGranted,
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: item.granted ? Colors.green : Colors.red,
                    ),
              ),
      ),
    );
  }
}

enum _PermissionType { runtime, installTime }

class _PermissionItem {
  _PermissionItem({
    required this.name,
    required this.description,
    required this.type,
    this.critical = false,
    this.check,
    this.request,
    this.platformGuard,
  });

  final String name;
  final String description;
  final _PermissionType type;
  final bool critical;
  final Future<bool> Function()? check;
  final Future<bool> Function()? request;
  final bool Function()? platformGuard;
  bool granted = false;
}
