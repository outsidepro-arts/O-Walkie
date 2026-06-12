import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../a11y/settings_section.dart';
import '../../data/audio_device_store.dart';
import '../../data/microphone_source_store.dart';
import '../../data/audio_settings_store.dart';
import '../../data/orientation_store.dart';
import '../../data/windows_settings_store.dart';
import '../../domain/microphone_source_option.dart';
import '../../features/home/home_screen_controller.dart';
import '../../features/home/home_screen_state.dart';
import '../../platform/audio_device_service.dart';
import '../../data/vibration_imitation_store.dart';
import '../../platform/haptics.dart';
import '../../platform/microphone_source_service.dart';
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
  List<NativeAudioDevice> _inputDevices = [];
  List<NativeAudioDevice> _outputDevices = [];
  int _inputDeviceIndex = -1;
  int _outputDeviceIndex = -1;
  List<MicrophoneSourceOption> _microphoneSources = [];
  String _selectedMicrophoneSourceId = MicrophoneSourceStore.defaultId;
  bool _ready = false;

  ProviderSubscription<HomeScreenState>? _sessionListener;

  @override
  void initState() {
    super.initState();
    _sessionListener = ref.listenManual(
      homeScreenControllerProvider,
      (previous, next) {
        if (next.sessionSupported && previous?.sessionSupported != true) {
          unawaited(_reloadAudioDevices());
        }
      },
    );
    _load();
  }

  @override
  void dispose() {
    _sessionListener?.close();
    super.dispose();
  }

  Future<void> _reloadAudioDevices() async {
    final snapshot = await _loadAudioDeviceSnapshot();
    if (!mounted || snapshot == null) {
      return;
    }
    setState(() {
      _microphoneSources = snapshot.microphoneSources;
      _selectedMicrophoneSourceId = snapshot.selectedMicrophoneSourceId;
      _inputDevices = snapshot.inputDevices;
      _outputDevices = snapshot.outputDevices;
      _inputDeviceIndex = snapshot.inputDeviceIndex;
      _outputDeviceIndex = snapshot.outputDeviceIndex;
    });
  }

  Future<_AudioDeviceSnapshot?> _loadAudioDeviceSnapshot() async {
    if (MicrophoneSourceService.isSupported) {
      final micStore = ref.read(microphoneSourceStoreProvider);
      final options = await MicrophoneSourceService.listOptions();
      var selectedId = micStore.selectedId();
      if (options.isNotEmpty && !options.any((o) => o.id == selectedId)) {
        selectedId = options.first.id;
        await micStore.setSelectedId(selectedId);
      }
      return _AudioDeviceSnapshot(
        microphoneSources: options,
        selectedMicrophoneSourceId: selectedId,
      );
    }
    if (!AudioDeviceService.showsPhysicalAudioDevices) {
      return const _AudioDeviceSnapshot();
    }
    final audioDeviceStore = ref.read(audioDeviceStoreProvider);
    final inputDevices = await AudioDeviceService.listInputDevices();
    final outputDevices = await AudioDeviceService.listOutputDevices();
    var inputIndex = audioDeviceStore.inputDeviceIndex();
    var outputIndex = audioDeviceStore.outputDeviceIndex();
    if (AudioDeviceService.supportsSystemDefaultOption) {
      if (inputIndex >= 0 &&
          !inputDevices.any((d) => d.index == inputIndex)) {
        inputIndex = AudioDeviceService.systemDefaultIndex;
        await audioDeviceStore.setInputDevice(index: inputIndex);
      }
      if (outputIndex >= 0 &&
          !outputDevices.any((d) => d.index == outputIndex)) {
        outputIndex = AudioDeviceService.systemDefaultIndex;
        await audioDeviceStore.setOutputDevice(index: outputIndex);
      }
    } else if (inputDevices.isNotEmpty && outputDevices.isNotEmpty) {
      final inputDevice = AudioDeviceService.resolveStoredDevice(
        audioDeviceStore,
        inputDevices,
        input: true,
      );
      final outputDevice = AudioDeviceService.resolveStoredDevice(
        audioDeviceStore,
        outputDevices,
        input: false,
      );
      inputIndex = inputDevice.index;
      outputIndex = outputDevice.index;
      if (audioDeviceStore.inputDeviceIndex() != inputIndex ||
          audioDeviceStore.inputPlatformId() != inputDevice.platformId) {
        await audioDeviceStore.setInputDevice(
          index: inputIndex,
          platformId: inputDevice.platformId,
        );
      }
      if (audioDeviceStore.outputDeviceIndex() != outputIndex ||
          audioDeviceStore.outputPlatformId() != outputDevice.platformId) {
        await audioDeviceStore.setOutputDevice(
          index: outputIndex,
          platformId: outputDevice.platformId,
        );
      }
    }
    return _AudioDeviceSnapshot(
      inputDevices: inputDevices,
      outputDevices: outputDevices,
      inputDeviceIndex: inputIndex,
      outputDeviceIndex: outputIndex,
    );
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
    final audioSnapshot = await _loadAudioDeviceSnapshot();
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
      if (audioSnapshot != null) {
        _microphoneSources = audioSnapshot.microphoneSources;
        _selectedMicrophoneSourceId = audioSnapshot.selectedMicrophoneSourceId;
        _inputDevices = audioSnapshot.inputDevices;
        _outputDevices = audioSnapshot.outputDevices;
        _inputDeviceIndex = audioSnapshot.inputDeviceIndex;
        _outputDeviceIndex = audioSnapshot.outputDeviceIndex;
      }
      _ready = true;
    });
    if (Haptics.showsDesktopSettings) {
      Haptics.applyFromStore(ref.read(vibrationImitationStoreProvider));
    }
  }

  int _dropdownDeviceIndex(int index, List<NativeAudioDevice> devices) {
    return AudioDeviceService.dropdownValue(index, devices);
  }

  List<DropdownMenuItem<int>> _audioDeviceMenuItems(List<NativeAudioDevice> devices) {
    return [
      if (AudioDeviceService.supportsSystemDefaultOption)
        DropdownMenuItem(
          value: AudioDeviceService.systemDefaultIndex,
          child: Text(AppStrings.settingsAudioDeviceDefault),
        ),
      for (final d in devices)
        DropdownMenuItem(
          value: d.index,
          child: Text(d.name),
        ),
    ];
  }

  Future<void> _setMicrophoneSource(String? id) async {
    if (id == null) {
      return;
    }
    final option = _microphoneSources.firstWhere(
      (o) => o.id == id,
      orElse: () => _microphoneSources.first,
    );
    await MicrophoneSourceService.persistAndApply(
      store: ref.read(microphoneSourceStoreProvider),
      option: option,
      bluetoothHeadset: ref.read(bluetoothHeadsetStoreProvider).isEnabled(),
    );
    setState(() => _selectedMicrophoneSourceId = id);
  }

  Future<void> _setInputDevice(int? index) async {
    if (index == null) {
      return;
    }
    await AudioDeviceService.persistAndApplyInput(
      store: ref.read(audioDeviceStoreProvider),
      index: index,
      inputDevices: _inputDevices,
      outputDevices: _outputDevices,
      outputIndex: _outputDeviceIndex,
    );
    setState(() => _inputDeviceIndex = index);
  }

  Future<void> _setOutputDevice(int? index) async {
    if (index == null) {
      return;
    }
    await AudioDeviceService.persistAndApplyOutput(
      store: ref.read(audioDeviceStoreProvider),
      index: index,
      inputDevices: _inputDevices,
      outputDevices: _outputDevices,
      inputIndex: _inputDeviceIndex,
    );
    setState(() => _outputDeviceIndex = index);
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
      await NativePlatform.prepareAudioSession(
        bluetoothHeadset: enabled,
        microphoneProfileId: ref.read(microphoneSourceStoreProvider).selectedId(),
      );
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
    if (!_ready) {
      return Scaffold(
        appBar: AppBar(
          title: Text(AppStrings.settingsTitle),
          leading: IconButton(
            icon: const Icon(Icons.arrow_back),
            onPressed: () => context.pop(),
          ),
        ),
        body: const Center(child: CircularProgressIndicator()),
      );
    }

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
          SettingsSection(
            title: AppStrings.settingsDisplay,
            children: [
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
            ],
          ),
          SettingsSection(
            title: AppStrings.settingsAudio,
            children: [
              if (MicrophoneSourceService.isSupported &&
                  _microphoneSources.isNotEmpty)
                DropdownButtonFormField<String>(
                  value: _selectedMicrophoneSourceId,
                  decoration: InputDecoration(
                    labelText: AppStrings.settingsAudioInputDevice,
                  ),
                  items: [
                    for (final option in _microphoneSources)
                      DropdownMenuItem(
                        value: option.id,
                        child: Text(option.displayTitle),
                      ),
                  ],
                  onChanged: _setMicrophoneSource,
                ),
              if (AudioDeviceService.showsPhysicalAudioDevices &&
                  _inputDevices.isNotEmpty) ...[
                if (MicrophoneSourceService.isSupported &&
                    _microphoneSources.isNotEmpty)
                  const SizedBox(height: 8),
                DropdownButtonFormField<int>(
                  value: _dropdownDeviceIndex(_inputDeviceIndex, _inputDevices),
                  decoration: InputDecoration(
                    labelText: AppStrings.settingsAudioInputDevice,
                  ),
                  items: _audioDeviceMenuItems(_inputDevices),
                  onChanged: _setInputDevice,
                ),
              ],
              if (AudioDeviceService.showsPhysicalAudioDevices &&
                  _outputDevices.isNotEmpty) ...[
                const SizedBox(height: 8),
                DropdownButtonFormField<int>(
                  value: _dropdownDeviceIndex(_outputDeviceIndex, _outputDevices),
                  decoration: InputDecoration(
                    labelText: AppStrings.settingsAudioOutputDevice,
                  ),
                  items: _audioDeviceMenuItems(_outputDevices),
                  onChanged: _setOutputDevice,
                ),
              ],
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
              if (Haptics.showsDesktopSettings)
                ListTile(
                  title: Text(AppStrings.settingsVibrationImitation),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: () => context.push('/settings/vibration-imitation'),
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
            ],
          ),
          if (NativePlatform.isWindows)
            SettingsSection(
              title: AppStrings.settingsWindows,
              children: [
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
            ),
          SettingsSection(
            title: AppStrings.rogerSignalLabel,
            children: [
              if (_rogerPatterns.isNotEmpty)
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Expanded(
                      child: DropdownButtonFormField<String>(
                        value: _selectedRogerId ?? _rogerPatterns.first.id,
                        decoration: InputDecoration(
                          labelText: AppStrings.rogerSignalLabel,
                        ),
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
                      _rogerPatterns.any(
                        (p) => p.id == _selectedRogerId && !p.builtIn,
                      ))
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
            ],
          ),
          SettingsSection(
            title: AppStrings.callSignalLabel,
            children: [
              if (_callingPatterns.isNotEmpty)
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Expanded(
                      child: DropdownButtonFormField<String>(
                        value: _selectedCallingId ?? _callingPatterns.first.id,
                        decoration: InputDecoration(
                          labelText: AppStrings.callSignalLabel,
                        ),
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
                      _callingPatterns.any(
                        (p) => p.id == _selectedCallingId && !p.builtIn,
                      ))
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
            ],
          ),
          SettingsSection(
            title: AppStrings.settingsAbout,
            spacingAfter: 0,
            children: [
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

class _AudioDeviceSnapshot {
  const _AudioDeviceSnapshot({
    this.microphoneSources = const [],
    this.selectedMicrophoneSourceId = MicrophoneSourceStore.defaultId,
    this.inputDevices = const [],
    this.outputDevices = const [],
    this.inputDeviceIndex = -1,
    this.outputDeviceIndex = -1,
  });

  final List<MicrophoneSourceOption> microphoneSources;
  final String selectedMicrophoneSourceId;
  final List<NativeAudioDevice> inputDevices;
  final List<NativeAudioDevice> outputDevices;
  final int inputDeviceIndex;
  final int outputDeviceIndex;
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
