import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../data/audio_settings_store.dart';
import '../../data/orientation_store.dart';
import '../../data/windows_settings_store.dart';
import '../../features/home/home_screen_controller.dart';
import '../../platform/native_platform.dart';
import '../../platform/windows/desktop_shell.dart';
import '../../platform/windows/windows_global_ptt.dart';
import '../../data/signal_pattern_store.dart';
import '../../domain/signal_pattern.dart';
import '../../domain/windows_ptt_binding.dart';
import '../../l10n/app_strings.dart';

const _githubUrl = 'https://github.com/outsidepro-arts/O-Walkie';

class SettingsScreen extends ConsumerStatefulWidget {
  const SettingsScreen({super.key});

  @override
  ConsumerState<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends ConsumerState<SettingsScreen> {
  PackageInfo? _packageInfo;
  ScreenOrientationMode _orientation = ScreenOrientationMode.followSystem;
  List<SignalPattern> _rogerPatterns = [];
  List<SignalPattern> _callingPatterns = [];
  String? _selectedRogerId;
  String? _selectedCallingId;
  bool _pauseDuringPhoneCall = true;
  bool _useBluetoothHeadset = false;
  bool _mediaButtonPtt = true;
  bool _externalControl = false;
  HardwarePttBinding _hardwarePttBinding = const HardwarePttBinding.unassigned();
  WindowsPttBinding? _globalPttBinding;
  bool _minimizeToTray = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final info = await PackageInfo.fromPlatform();
    final orientation = await ref.read(orientationStoreProvider).load();
    final rogerStore = ref.read(rogerPatternStoreProvider);
    final callingStore = ref.read(callingPatternStoreProvider);
    final phoneCallPause = ref.read(phoneCallPauseStoreProvider);
    final bluetoothHeadset = ref.read(bluetoothHeadsetStoreProvider);
    final mediaButtonPtt = ref.read(mediaButtonPttStoreProvider);
    final externalControl = ref.read(externalControlStoreProvider);
    final hardwareBinding = await NativePlatform.getHardwarePttBinding();
    final externalControlEnabled = await externalControl.isEnabled();
    final windowsStore = ref.read(windowsSettingsStoreProvider);
    final globalBinding = windowsStore.loadBinding();
    final minimizeToTray = windowsStore.minimizeToTrayOnClose();
    if (!mounted) {
      return;
    }
    setState(() {
      _packageInfo = info;
      _orientation = orientation;
      _rogerPatterns = rogerStore.getAllPatterns();
      _callingPatterns = callingStore.getAllPatterns();
      _selectedRogerId = rogerStore.getSelectedPattern().id;
      _selectedCallingId = callingStore.getSelectedPattern().id;
      _pauseDuringPhoneCall = phoneCallPause.isEnabled();
      _useBluetoothHeadset = bluetoothHeadset.isEnabled();
      _mediaButtonPtt = mediaButtonPtt.isEnabled();
      _externalControl = externalControlEnabled;
      _hardwarePttBinding = hardwareBinding;
      _globalPttBinding = globalBinding;
      _minimizeToTray = minimizeToTray;
    });
  }

  Future<void> _setPauseDuringPhoneCall(bool? enabled) async {
    if (enabled == null) {
      return;
    }
    await ref.read(phoneCallPauseStoreProvider).setEnabled(enabled);
    setState(() => _pauseDuringPhoneCall = enabled);
  }

  Future<void> _setUseBluetoothHeadset(bool? enabled) async {
    if (enabled == null) {
      return;
    }
    await ref.read(bluetoothHeadsetStoreProvider).setEnabled(enabled);
    setState(() => _useBluetoothHeadset = enabled);
    if (NativePlatform.isMobile) {
      await NativePlatform.prepareAudioSession(bluetoothHeadset: enabled);
    }
  }

  Future<void> _setExternalControl(bool? enabled) async {
    if (enabled == null) {
      return;
    }
    await ref.read(externalControlStoreProvider).setEnabled(enabled);
    setState(() => _externalControl = enabled);
  }

  Future<void> _setMediaButtonPtt(bool? enabled) async {
    if (enabled == null) {
      return;
    }
    await ref.read(mediaButtonPttStoreProvider).setEnabled(enabled);
    setState(() => _mediaButtonPtt = enabled);
    if (NativePlatform.isAndroid) {
      final connected = ref.read(homeScreenControllerProvider).isConnected;
      await NativePlatform.syncPttMediaSession(active: enabled && connected);
    }
  }

  String _hardwarePttLabel() {
    if (!_hardwarePttBinding.assigned) {
      return AppStrings.settingsHardwarePttUnassigned;
    }
    if (_hardwarePttBinding.scanCode > 0) {
      return 'Scan code ${_hardwarePttBinding.scanCode}';
    }
    return 'Key code ${_hardwarePttBinding.keyCode}';
  }

  Future<void> _setMinimizeToTray(bool? enabled) async {
    if (enabled == null) {
      return;
    }
    await ref.read(desktopShellProvider).setMinimizeToTrayOnClose(enabled);
    setState(() => _minimizeToTray = enabled);
  }

  Future<void> _showGlobalPttHotkeyDialog() async {
    if (!NativePlatform.isWindows) {
      return;
    }
    final recorded = await showDialog<WindowsPttBinding?>(
      context: context,
      barrierDismissible: false,
      builder: (dialogContext) => const _GlobalPttHotkeyDialog(),
    );
    if (!mounted || recorded == null) {
      return;
    }
    await ref.read(desktopShellProvider).setGlobalPttBinding(recorded);
    setState(() => _globalPttBinding = recorded);
  }

  Future<void> _clearGlobalPttHotkey() async {
    await ref.read(desktopShellProvider).setGlobalPttBinding(null);
    if (!mounted) {
      return;
    }
    setState(() => _globalPttBinding = null);
  }

  Future<void> _showHardwarePttDialog() async {
    if (!NativePlatform.isAndroid) {
      return;
    }
    await showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (dialogContext) => _HardwarePttKeyDialog(
        initialBinding: _hardwarePttBinding,
        onBound: (binding) {
          if (!mounted) {
            return;
          }
          setState(() => _hardwarePttBinding = binding);
        },
      ),
    );
    final binding = await NativePlatform.getHardwarePttBinding();
    if (!mounted) {
      return;
    }
    setState(() => _hardwarePttBinding = binding);
  }

  Future<void> _reloadPatterns() async {
    final rogerStore = ref.read(rogerPatternStoreProvider);
    final callingStore = ref.read(callingPatternStoreProvider);
    setState(() {
      _rogerPatterns = rogerStore.getAllPatterns();
      _callingPatterns = callingStore.getAllPatterns();
      _selectedRogerId = rogerStore.getSelectedPattern().id;
      _selectedCallingId = callingStore.getSelectedPattern().id;
    });
  }

  SignalPattern? _rogerPatternById(String? id) {
    if (id == null) {
      return null;
    }
    for (final p in _rogerPatterns) {
      if (p.id == id) {
        return p;
      }
    }
    return null;
  }

  SignalPattern? _callingPatternById(String? id) {
    if (id == null) {
      return null;
    }
    for (final p in _callingPatterns) {
      if (p.id == id) {
        return p;
      }
    }
    return null;
  }

  void _playSelectedRoger() {
    final pattern = _rogerPatternById(_selectedRogerId) ??
        (_rogerPatterns.isEmpty ? null : _rogerPatterns.first);
    if (pattern == null) {
      return;
    }
    ref
        .read(homeScreenControllerProvider.notifier)
        .previewSignalPattern(pattern.points);
  }

  void _playSelectedCall() {
    final pattern = _callingPatternById(_selectedCallingId) ??
        (_callingPatterns.isEmpty ? null : _callingPatterns.first);
    if (pattern == null) {
      return;
    }
    ref
        .read(homeScreenControllerProvider.notifier)
        .previewSignalPattern(pattern.expandedPoints());
  }

  Future<void> _setOrientation(ScreenOrientationMode? mode) async {
    if (mode == null) {
      return;
    }
    await ref.read(orientationStoreProvider).save(mode);
    if (!mounted) {
      return;
    }
    setState(() => _orientation = mode);
  }

  Future<void> _openGitHub() async {
    final uri = Uri.parse(_githubUrl);
    if (!await launchUrl(uri, mode: LaunchMode.externalApplication)) {
      if (!mounted) {
        return;
      }
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.settingsGitHubOpenFailed)),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final version = _packageInfo?.version ?? '…';
    final build = _packageInfo?.buildNumber ?? '';

    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.settingsTitle),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => context.pop(),
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text(
            AppStrings.settingsDisplay,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          DropdownButtonFormField<ScreenOrientationMode>(
            value: _orientation,
            decoration: InputDecoration(
              labelText: AppStrings.settingsOrientation,
            ),
            items: ScreenOrientationMode.values
                .map(
                  (m) => DropdownMenuItem(
                    value: m,
                    child: Text(_orientationLabel(m)),
                  ),
                )
                .toList(),
            onChanged: _setOrientation,
          ),
          const SizedBox(height: 24),
          Text(
            AppStrings.settingsAudio,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          SwitchListTile(
            title: Text(AppStrings.settingsPauseDuringPhoneCall),
            value: _pauseDuringPhoneCall,
            onChanged: _setPauseDuringPhoneCall,
          ),
          SwitchListTile(
            title: Text(AppStrings.settingsUseBluetoothHeadset),
            value: _useBluetoothHeadset,
            onChanged: _setUseBluetoothHeadset,
          ),
          if (NativePlatform.isAndroid) ...[
            SwitchListTile(
              title: Text(AppStrings.settingsMediaButtonPtt),
              value: _mediaButtonPtt,
              onChanged: _setMediaButtonPtt,
            ),
            ListTile(
              title: Text(AppStrings.settingsHardwarePttKey),
              subtitle: Text(_hardwarePttLabel()),
              trailing: TextButton(
                onPressed: _showHardwarePttDialog,
                child: Text(AppStrings.settingsHardwarePttAssign),
              ),
            ),
            SwitchListTile(
              title: Text(AppStrings.settingsExternalControl),
              value: _externalControl,
              onChanged: _setExternalControl,
            ),
          ],
          if (NativePlatform.isWindows) ...[
            const SizedBox(height: 24),
            Text(
              AppStrings.settingsWindows,
              style: Theme.of(context).textTheme.titleSmall,
            ),
            ListTile(
              title: Text(AppStrings.settingsGlobalPttHotkey),
              subtitle: Text(
                _globalPttBinding?.displayName ??
                    AppStrings.settingsGlobalPttHotkeyUnassigned,
              ),
              trailing: Wrap(
                spacing: 4,
                children: [
                  TextButton(
                    onPressed: _showGlobalPttHotkeyDialog,
                    child: Text(AppStrings.settingsGlobalPttHotkeyAssign),
                  ),
                  if (_globalPttBinding != null)
                    TextButton(
                      onPressed: _clearGlobalPttHotkey,
                      child: Text(AppStrings.settingsGlobalPttHotkeyClear),
                    ),
                ],
              ),
            ),
            SwitchListTile(
              title: Text(AppStrings.settingsMinimizeToTray),
              value: _minimizeToTray,
              onChanged: _setMinimizeToTray,
            ),
          ],
          const SizedBox(height: 24),
          Text(
            AppStrings.rogerSignalLabel,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: DropdownButtonFormField<String>(
                  value: _selectedRogerId ??
                      (_rogerPatterns.isEmpty ? null : _rogerPatterns.first.id),
                  decoration:
                      InputDecoration(labelText: AppStrings.rogerSignalLabel),
                  items: [
                    for (final p in _rogerPatterns)
                      DropdownMenuItem(value: p.id, child: Text(p.name)),
                  ],
                  onChanged: (id) async {
                    if (id == null) {
                      return;
                    }
                    await ref
                        .read(rogerPatternStoreProvider)
                        .setSelectedPattern(id);
                    setState(() => _selectedRogerId = id);
                  },
                ),
              ),
              const SizedBox(width: 8),
              TextButton(
                onPressed: _playSelectedRoger,
                child: Text(AppStrings.playSignalButton),
              ),
            ],
          ),
          Row(
            children: [
              TextButton(
                onPressed: () async {
                  await context.push('/signals/roger/edit');
                  await _reloadPatterns();
                },
                child: Text(AppStrings.rogerCustomButton),
              ),
              if (_selectedRogerId != null &&
                  _rogerPatterns.any((p) => p.id == _selectedRogerId && !p.builtIn))
                TextButton(
                  onPressed: () async {
                    await context.push(
                      '/signals/roger/edit?id=$_selectedRogerId',
                    );
                    await _reloadPatterns();
                  },
                  child: Text(AppStrings.rogerEditSegment),
                ),
            ],
          ),
          const SizedBox(height: 16),
          Text(
            AppStrings.callSignalLabel,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: DropdownButtonFormField<String>(
                  value: _selectedCallingId ??
                      (_callingPatterns.isEmpty
                          ? null
                          : _callingPatterns.first.id),
                  decoration:
                      InputDecoration(labelText: AppStrings.callSignalLabel),
                  items: [
                    for (final p in _callingPatterns)
                      DropdownMenuItem(value: p.id, child: Text(p.name)),
                  ],
                  onChanged: (id) async {
                    if (id == null) {
                      return;
                    }
                    await ref
                        .read(callingPatternStoreProvider)
                        .setSelectedPattern(id);
                    setState(() => _selectedCallingId = id);
                  },
                ),
              ),
              const SizedBox(width: 8),
              TextButton(
                onPressed: _playSelectedCall,
                child: Text(AppStrings.playSignalButton),
              ),
            ],
          ),
          Row(
            children: [
              TextButton(
                onPressed: () async {
                  await context.push('/signals/calling/edit');
                  await _reloadPatterns();
                },
                child: Text(AppStrings.rogerCustomButton),
              ),
              if (_selectedCallingId != null &&
                  _callingPatterns.any((p) => p.id == _selectedCallingId && !p.builtIn))
                TextButton(
                  onPressed: () async {
                    await context.push(
                      '/signals/calling/edit?id=$_selectedCallingId',
                    );
                    await _reloadPatterns();
                  },
                  child: Text(AppStrings.rogerEditSegment),
                ),
            ],
          ),
          const SizedBox(height: 24),
          Text(
            AppStrings.settingsAbout,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          ListTile(
            title: Text(AppStrings.settingsAppVersion),
            subtitle: Text(build.isEmpty ? version : '$version ($build)'),
          ),
          ListTile(
            title: Text(AppStrings.settingsProtocolVersion),
            subtitle: const Text('2'),
          ),
          ListTile(
            title: Text(AppStrings.settingsGitHub),
            trailing: const Icon(Icons.open_in_new),
            onTap: _openGitHub,
          ),
        ],
      ),
    );
  }

  String _orientationLabel(ScreenOrientationMode mode) {
    return switch (mode) {
      ScreenOrientationMode.followSystem => AppStrings.orientationFollowSystem,
      ScreenOrientationMode.portrait => AppStrings.orientationPortrait,
      ScreenOrientationMode.landscape => AppStrings.orientationLandscape,
    };
  }
}

class _GlobalPttHotkeyDialog extends StatefulWidget {
  const _GlobalPttHotkeyDialog();

  @override
  State<_GlobalPttHotkeyDialog> createState() => _GlobalPttHotkeyDialogState();
}

class _GlobalPttHotkeyDialogState extends State<_GlobalPttHotkeyDialog> {
  StreamSubscription<String>? _captureSub;
  String _status = AppStrings.settingsGlobalPttHotkeyDialogWaiting;

  @override
  void initState() {
    super.initState();
    _beginCapture();
  }

  Future<void> _beginCapture() async {
    _captureSub = WindowsGlobalPtt.events.listen((event) async {
      if (event != WindowsGlobalPtt.capturedEvent || !mounted) {
        return;
      }
      final binding = await WindowsGlobalPtt.takeCaptureResult();
      if (!mounted || binding == null) {
        return;
      }
      Navigator.of(context).pop(binding);
    });
    await WindowsGlobalPtt.startCapture();
  }

  @override
  void dispose() {
    unawaited(WindowsGlobalPtt.cancelCapture());
    unawaited(_captureSub?.cancel());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(AppStrings.settingsGlobalPttHotkeyDialogTitle),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(AppStrings.settingsGlobalPttHotkeyDialogHint),
          const SizedBox(height: 16),
          Text(_status),
        ],
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(AppStrings.rogerCancel),
        ),
      ],
    );
  }
}

class _HardwarePttKeyDialog extends StatefulWidget {
  const _HardwarePttKeyDialog({
    required this.initialBinding,
    required this.onBound,
  });

  final HardwarePttBinding initialBinding;
  final ValueChanged<HardwarePttBinding> onBound;

  @override
  State<_HardwarePttKeyDialog> createState() => _HardwarePttKeyDialogState();
}

class _HardwarePttKeyDialogState extends State<_HardwarePttKeyDialog> {
  StreamSubscription<String>? _platformSub;
  HardwarePttBinding _binding = const HardwarePttBinding.unassigned();
  bool _capturing = false;

  @override
  void initState() {
    super.initState();
    _binding = widget.initialBinding;
    _startCapture();
  }

  Future<void> _startCapture() async {
    _platformSub = NativePlatform.platformEvents.listen((event) async {
      if (event != NativePlatform.hardwarePttBoundEvent) {
        return;
      }
      final binding = await NativePlatform.getHardwarePttBinding();
      if (!mounted) {
        return;
      }
      setState(() {
        _binding = binding;
        _capturing = false;
      });
      widget.onBound(binding);
    });
    setState(() => _capturing = true);
    await NativePlatform.startCaptureHardwarePttKey();
  }

  @override
  void dispose() {
    unawaited(NativePlatform.cancelCaptureHardwarePttKey());
    _platformSub?.cancel();
    super.dispose();
  }

  Future<void> _resetBinding() async {
    await NativePlatform.clearHardwarePttBinding();
    if (!mounted) {
      return;
    }
    setState(() {
      _binding = const HardwarePttBinding.unassigned();
      _capturing = true;
    });
    widget.onBound(const HardwarePttBinding.unassigned());
    await NativePlatform.startCaptureHardwarePttKey();
  }

  String _bindingLabel() {
    if (!_binding.assigned) {
      return AppStrings.settingsHardwarePttDialogWaiting;
    }
    if (_binding.scanCode > 0) {
      return 'Scan code ${_binding.scanCode}';
    }
    return 'Key code ${_binding.keyCode}';
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(AppStrings.settingsHardwarePttDialogTitle),
      content: Text(_capturing && !_binding.assigned
          ? AppStrings.settingsHardwarePttDialogWaiting
          : _bindingLabel()),
      actions: [
        TextButton(
          onPressed: _resetBinding,
          child: Text(AppStrings.settingsHardwarePttReset),
        ),
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(AppStrings.commonOk),
        ),
      ],
    );
  }
}
