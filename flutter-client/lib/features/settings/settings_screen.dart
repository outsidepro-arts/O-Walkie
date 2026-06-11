import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../data/orientation_store.dart';
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

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final info = await PackageInfo.fromPlatform();
    final orientation = await ref.read(orientationStoreProvider).load();
    if (!mounted) {
      return;
    }
    setState(() {
      _packageInfo = info;
      _orientation = orientation;
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
