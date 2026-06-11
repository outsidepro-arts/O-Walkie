import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../data/audio_settings_store.dart';
import '../../data/orientation_store.dart';
import '../../platform/native_platform.dart';
import '../../data/signal_pattern_store.dart';
import '../../domain/signal_pattern.dart';
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
        const SnackBar(content: Text('Could not open GitHub')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final version = _packageInfo?.version ?? '…';
    final build = _packageInfo?.buildNumber ?? '';

    return Scaffold(
      appBar: AppBar(
        title: const Text(AppStrings.settingsTitle),
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
            decoration: const InputDecoration(
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
            title: const Text(AppStrings.settingsPauseDuringPhoneCall),
            value: _pauseDuringPhoneCall,
            onChanged: _setPauseDuringPhoneCall,
          ),
          SwitchListTile(
            title: const Text(AppStrings.settingsUseBluetoothHeadset),
            value: _useBluetoothHeadset,
            onChanged: _setUseBluetoothHeadset,
          ),
          const SizedBox(height: 24),
          Text(
            AppStrings.rogerSignalLabel,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          DropdownButtonFormField<String>(
            value: _selectedRogerId ??
                (_rogerPatterns.isEmpty ? null : _rogerPatterns.first.id),
            decoration: const InputDecoration(labelText: AppStrings.rogerSignalLabel),
            items: [
              for (final p in _rogerPatterns)
                DropdownMenuItem(value: p.id, child: Text(p.name)),
            ],
            onChanged: (id) async {
              if (id == null) {
                return;
              }
              await ref.read(rogerPatternStoreProvider).setSelectedPattern(id);
              setState(() => _selectedRogerId = id);
            },
          ),
          Row(
            children: [
              TextButton(
                onPressed: () async {
                  await context.push('/signals/roger/edit');
                  await _reloadPatterns();
                },
                child: const Text(AppStrings.rogerCustomButton),
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
                  child: const Text(AppStrings.rogerEditSegment),
                ),
            ],
          ),
          const SizedBox(height: 16),
          Text(
            AppStrings.callSignalLabel,
            style: Theme.of(context).textTheme.titleSmall,
          ),
          const SizedBox(height: 8),
          DropdownButtonFormField<String>(
            value: _selectedCallingId ??
                (_callingPatterns.isEmpty ? null : _callingPatterns.first.id),
            decoration: const InputDecoration(labelText: AppStrings.callSignalLabel),
            items: [
              for (final p in _callingPatterns)
                DropdownMenuItem(value: p.id, child: Text(p.name)),
            ],
            onChanged: (id) async {
              if (id == null) {
                return;
              }
              await ref.read(callingPatternStoreProvider).setSelectedPattern(id);
              setState(() => _selectedCallingId = id);
            },
          ),
          Row(
            children: [
              TextButton(
                onPressed: () async {
                  await context.push('/signals/calling/edit');
                  await _reloadPatterns();
                },
                child: const Text(AppStrings.rogerCustomButton),
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
                  child: const Text(AppStrings.rogerEditSegment),
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
