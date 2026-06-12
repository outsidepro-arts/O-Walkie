import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../a11y/a11y_desktop_numeric_field.dart';
import '../../a11y/settings_section.dart';
import '../../data/vibration_imitation_store.dart';
import '../../l10n/app_strings.dart';
import '../../platform/haptics.dart';
import '../../platform/vibration_imitation.dart';

/// Desktop vibration imitation tuning.
///
/// Spin boxes only — no [Slider] on this screen (Windows Narrator / AXTree).
class VibrationImitationSettingsScreen extends ConsumerStatefulWidget {
  const VibrationImitationSettingsScreen({super.key});

  @override
  ConsumerState<VibrationImitationSettingsScreen> createState() =>
      _VibrationImitationSettingsScreenState();
}

class _VibrationImitationSettingsScreenState
    extends ConsumerState<VibrationImitationSettingsScreen> {
  late int _hz;
  late int _volume;

  @override
  void initState() {
    super.initState();
    final store = ref.read(vibrationImitationStoreProvider);
    _hz = store.freqHz().round();
    _volume = store.volumePercent();
  }

  Future<void> _persistHz(int hz) async {
    final store = ref.read(vibrationImitationStoreProvider);
    await store.setFreqHz(hz.toDouble());
    Haptics.applyFromStore(store);
    if (!mounted) {
      return;
    }
    setState(() => _hz = store.freqHz().round());
  }

  Future<void> _persistVolume(int volume) async {
    final store = ref.read(vibrationImitationStoreProvider);
    await store.setVolumePercent(volume);
    Haptics.applyFromStore(store);
    if (!mounted) {
      return;
    }
    setState(() => _volume = store.volumePercent());
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.settingsVibrationImitation),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => context.pop(),
        ),
      ),
      body: SafeArea(
        child: FocusTraversalGroup(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                SettingsSection(
                  title: AppStrings.settingsVibrationImitationFrequency,
                  children: [
                    A11yDesktopNumericField(
                      value: _hz,
                      min: VibrationImitation.minFreqHz.round(),
                      max: VibrationImitation.maxFreqHz.round(),
                      title: AppStrings.settingsVibrationImitationFrequency,
                      suffix: 'Hz',
                      onChanged: (hz) => setState(() => _hz = hz),
                      onCommit: _persistHz,
                    ),
                  ],
                ),
                SettingsSection(
                  title: AppStrings.settingsVibrationImitationVolume,
                  children: [
                    A11yDesktopNumericField(
                      value: _volume,
                      min: 0,
                      max: 100,
                      title: AppStrings.settingsVibrationImitationVolume,
                      suffix: '%',
                      onChanged: (volume) => setState(() => _volume = volume),
                      onCommit: _persistVolume,
                    ),
                  ],
                ),
                SettingsSection(
                  title: AppStrings.settingsVibrationImitationPreview,
                  spacingAfter: 0,
                  children: [
                    ListTile(
                      title: Text(AppStrings.settingsVibrationImitationPreview),
                      trailing: const Icon(Icons.play_arrow),
                      onTap: () => Haptics.previewDesktopImitation(),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
