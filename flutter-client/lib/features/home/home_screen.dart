import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../a11y/a11y.dart';
import '../../domain/scan_mode.dart';
import '../../domain/server_profile.dart';
import '../../l10n/a11y_strings.dart';
import '../../l10n/app_strings.dart';
import 'home_screen_controller.dart';
import 'ptt_gesture_button.dart';
import 'session_event_mapper.dart';

class HomeScreen extends ConsumerStatefulWidget {
  const HomeScreen({super.key});

  @override
  ConsumerState<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends ConsumerState<HomeScreen> {
  late final TextEditingController _nameCtrl;
  late final TextEditingController _hostCtrl;
  late final TextEditingController _portCtrl;
  late final TextEditingController _channelCtrl;

  @override
  void initState() {
    super.initState();
    final draft = ref.read(homeScreenControllerProvider).draftProfile;
    _nameCtrl = TextEditingController(text: draft.name);
    _hostCtrl = TextEditingController(text: draft.host);
    _portCtrl = TextEditingController(text: '${draft.port}');
    _channelCtrl = TextEditingController(text: draft.channel);
    ref.listenManual(
      homeScreenControllerProvider.select((s) => s.draftProfile),
      (previous, next) {
        if (previous == next) {
          return;
        }
        _loadControllersFromProfile(next);
      },
    );
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) {
        return;
      }
      _loadControllersFromProfile(
        ref.read(homeScreenControllerProvider).draftProfile,
      );
    });
    ref.listenManual(
      homeScreenControllerProvider.select((s) => s.statusMessage),
      (previous, next) {
        if (next == AppStrings.connectionLinkImported) {
          _loadControllersFromProfile(
            ref.read(homeScreenControllerProvider).draftProfile,
          );
        }
      },
    );
  }

  void _loadControllersFromProfile(ServerProfile profile) {
    _nameCtrl.text = profile.name;
    _hostCtrl.text = profile.host;
    _portCtrl.text = '${profile.port}';
    _channelCtrl.text = profile.channel;
  }

  @override
  void dispose() {
    _nameCtrl.dispose();
    _hostCtrl.dispose();
    _portCtrl.dispose();
    _channelCtrl.dispose();
    super.dispose();
  }

  void _syncProfile() {
    ref.read(homeScreenControllerProvider.notifier).updateProfile(
          name: _nameCtrl.text,
          host: _hostCtrl.text,
          portText: _portCtrl.text,
          channel: _channelCtrl.text,
        );
  }

  void _onScanPressed(bool scanActive) {
    final controller = ref.read(homeScreenControllerProvider.notifier);
    if (scanActive) {
      controller.stopScanning();
      return;
    }
    showDialog<void>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text(AppStrings.scanModeTitle),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              title: Text(AppStrings.scanModeOneShot),
              onTap: () {
                Navigator.of(dialogContext).pop();
                controller.startScanning(ScanMode.oneShot);
              },
            ),
            ListTile(
              title: Text(AppStrings.scanModeContinuous),
              onTap: () {
                Navigator.of(dialogContext).pop();
                controller.startScanning(ScanMode.continuous);
              },
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _shareConnection() async {
    _syncProfile();
    final includeName = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text(AppStrings.shareConnection),
        content: Text(AppStrings.connectionLinkIncludeNamePrompt),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(dialogContext).pop(true),
            child: Text(AppStrings.commonYes),
          ),
          TextButton(
            onPressed: () => Navigator.of(dialogContext).pop(false),
            child: Text(AppStrings.commonNo),
          ),
        ],
      ),
    );
    if (includeName == null || !mounted) {
      return;
    }
    await ref
        .read(homeScreenControllerProvider.notifier)
        .copyConnectionLinkToClipboard(includeName: includeName);
  }

  Future<void> _importConnection() async {
    _syncProfile();
    await ref.read(homeScreenControllerProvider.notifier).importConnectionFromClipboard();
    if (!mounted) {
      return;
    }
    final profile = ref.read(homeScreenControllerProvider).draftProfile;
    _loadControllersFromProfile(profile);
  }

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(homeScreenControllerProvider);
    final controller = ref.read(homeScreenControllerProvider.notifier);
    final canConnect = state.sessionSupported;
    final connectLabel = state.isConnected || state.isConnecting
        ? AppStrings.disconnectServer
        : AppStrings.connectServer;

    final connectionChip = state.connectionDisplayChip;

    return Semantics(
      explicitChildNodes: true,
      child: Scaffold(
        body: SafeArea(
          child: FocusTraversalGroup(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              keyboardDismissBehavior: ScrollViewKeyboardDismissBehavior.onDrag,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  _HeaderRow(
                    repeaterEnabled: state.draftProfile.repeater,
                    onRepeaterToggled: controller.setRepeaterMode,
                  ),
                  const SizedBox(height: 8),
                  _StatusChips(
                    connection: connectionChip,
                    signal: state.signalChip,
                  ),
                  if (state.statusInfo != null) ...[
                    const SizedBox(height: 8),
                    Text(
                      state.statusInfo!,
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                  ],
                  if (state.statusMessage != null) ...[
                    const SizedBox(height: 8),
                    Text(
                      state.statusMessage!,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                            color: Theme.of(context).colorScheme.primary,
                          ),
                    ),
                  ],
                  if (state.lastError != null) ...[
                    const SizedBox(height: 8),
                    Semantics(
                      liveRegion: true,
                      child: Text(
                        state.lastError!,
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                              color: Theme.of(context).colorScheme.error,
                            ),
                      ),
                    ),
                  ],
                  const SizedBox(height: 12),
                  ExcludeSemantics(
                    child: Text(
                      AppStrings.serverProfiles,
                      style: Theme.of(context).textTheme.titleSmall?.copyWith(
                            fontWeight: FontWeight.bold,
                          ),
                    ),
                  ),
                  const SizedBox(height: 4),
                  DropdownButtonFormField<int>(
                    value: state.selectedServerIndex
                        .clamp(0, state.profiles.length - 1),
                    decoration: InputDecoration(
                      labelText: AppStrings.serverProfiles,
                      contentPadding:
                          const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                    ),
                      items: [
                        for (var i = 0; i < state.profiles.length; i++)
                          DropdownMenuItem(
                            value: i,
                            child: Text(
                              state.profiles[i].name.isEmpty
                                  ? AppStrings.profileNumberFallback(i + 1)
                                  : state.profiles[i].name,
                            ),
                          ),
                      ],
                      onChanged: state.canSelectProfiles
                          ? (index) {
                              if (index != null) {
                                controller.selectProfile(index);
                                _loadControllersFromProfile(
                                  ref
                                      .read(homeScreenControllerProvider)
                                      .profiles[index],
                                );
                              }
                            }
                          : null,
                  ),
                  const SizedBox(height: 8),
                  OutlinedButton(
                    onPressed: controller.toggleConnectionDetails,
                    child: Text(
                      state.connectionDetailsExpanded
                          ? AppStrings.collapseConnectionDetails
                          : AppStrings.expandConnectionDetails,
                    ),
                  ),
                  if (!state.connectionDetailsExpanded) ...[
                    const SizedBox(height: 8),
                    _CollapsedActions(
                      scanActive: state.scanActive,
                      onToggleScan: () => _onScanPressed(state.scanActive),
                      connectLabel: connectLabel,
                      canConnect: canConnect,
                      onConnect: canConnect
                          ? () {
                              unawaited(controller.connectToSelectedProfile());
                            }
                          : null,
                      onPrevious: state.canSwitchProfiles
                          ? controller.previousProfile
                          : null,
                      onNext: state.canSwitchProfiles
                          ? controller.nextProfile
                          : null,
                    ),
                  ],
                  if (state.connectionDetailsExpanded) ...[
                    const SizedBox(height: 8),
                    _ConnectionDetailsForm(
                      nameCtrl: _nameCtrl,
                      hostCtrl: _hostCtrl,
                      portCtrl: _portCtrl,
                      channelCtrl: _channelCtrl,
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: OutlinedButton(
                            onPressed: () async {
                              _syncProfile();
                              await controller.saveCurrentProfile();
                              _loadControllersFromProfile(
                                ref.read(homeScreenControllerProvider).profile,
                              );
                            },
                            child: Text(AppStrings.saveServer),
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: OutlinedButton(
                            onPressed: state.profiles.length > 1
                                ? () {
                                    _syncProfile();
                                    controller.deleteCurrentProfile();
                                  }
                                : null,
                            child: Text(AppStrings.deleteServer),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: OutlinedButton(
                            onPressed: _shareConnection,
                            child: Text(AppStrings.shareConnection),
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: OutlinedButton(
                            onPressed: _importConnection,
                            child: Text(AppStrings.importConnection),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    _ConnectButton(
                      label: connectLabel,
                      enabled: canConnect,
                      onPressed: canConnect
                          ? () {
                              _syncProfile();
                              controller.toggleConnection();
                            }
                          : null,
                    ),
                  ],
                  const SizedBox(height: 24),
                  _RxVolumeSection(
                    percent: state.rxVolumePercent,
                    onChanged: controller.setRxVolume,
                  ),
                  const SizedBox(height: 16),
                  _PttArea(
                    enabled: pttEnabled(
                      sessionSupported: state.sessionSupported,
                      isConnected: state.isConnected,
                      pttServerLocked: state.pttServerLocked,
                    ),
                    active: state.txActive,
                    label: pttButtonLabel(
                      txActive: state.txActive,
                      pttServerLocked: state.pttServerLocked,
                      pttLockSec: state.pttLockSec,
                      txCountdownSec: state.txCountdownSec,
                    ),
                    locked: state.pttServerLocked,
                    pttLockSec: state.pttLockSec,
                    sessionConnected: state.isConnected,
                    onPttDown: controller.pttDown,
                    onPttUp: controller.pttUp,
                    onCall: controller.sendCall,
                  ),
                  const SizedBox(height: 12),
                  ExcludeSemantics(
                    child: Text(
                      '${AppStrings.coreVersionFooter}: ${state.coreVersion} · '
                      '${AppStrings.protocolLabel} ${state.protocolVersion}',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _HeaderRow extends StatelessWidget {
  const _HeaderRow({
    required this.repeaterEnabled,
    required this.onRepeaterToggled,
  });

  final bool repeaterEnabled;
  final ValueChanged<bool> onRepeaterToggled;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: Semantics(
            header: true,
            child: Text(
              AppStrings.appName,
              style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
            ),
          ),
        ),
        Semantics(
          button: true,
          label: AppStrings.menuMore,
          excludeSemantics: true,
          child: PopupMenuButton<String>(
            onSelected: (value) {
              if (value == 'repeater') {
                onRepeaterToggled(!repeaterEnabled);
              } else if (value == 'settings') {
                context.push('/settings');
              }
            },
            itemBuilder: (context) => [
              CheckedPopupMenuItem(
                value: 'repeater',
                checked: repeaterEnabled,
                child: Text(AppStrings.menuRepeaterMode),
              ),
              PopupMenuItem(
                value: 'settings',
                child: Text(AppStrings.menuSettings),
              ),
            ],
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
              decoration: BoxDecoration(
                border: Border.all(color: Theme.of(context).colorScheme.outline),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(AppStrings.menuMore),
            ),
          ),
        ),
      ],
    );
  }
}

class _StatusChips extends StatelessWidget {
  const _StatusChips({required this.connection, required this.signal});

  final String connection;
  final String signal;

  @override
  Widget build(BuildContext context) {
    final chipStyle = Theme.of(context).textTheme.bodyMedium?.copyWith(
          fontWeight: FontWeight.bold,
        );
    return Row(
      children: [
        Expanded(
          child: Semantics(
            liveRegion: true,
            label: connection,
            excludeSemantics: true,
            child: _ChipBox(child: Text(connection, style: chipStyle)),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Semantics(
            label: signal,
            excludeSemantics: true,
            child: _ChipBox(
              alignment: Alignment.centerRight,
              child: Text(signal, style: chipStyle, textAlign: TextAlign.end),
            ),
          ),
        ),
      ],
    );
  }
}

class _ChipBox extends StatelessWidget {
  const _ChipBox({required this.child, this.alignment = Alignment.centerLeft});

  final Widget child;
  final Alignment alignment;

  @override
  Widget build(BuildContext context) {
    return ExcludeSemantics(
      child: Container(
        alignment: alignment,
        padding: const EdgeInsets.all(8),
        decoration: BoxDecoration(
          color: Colors.white.withValues(alpha: 0.1),
          borderRadius: BorderRadius.circular(4),
        ),
        child: child,
      ),
    );
  }
}

class _CollapsedActions extends StatelessWidget {
  const _CollapsedActions({
    required this.scanActive,
    required this.onToggleScan,
    required this.connectLabel,
    required this.canConnect,
    required this.onConnect,
    required this.onPrevious,
    required this.onNext,
  });

  final bool scanActive;
  final VoidCallback onToggleScan;
  final String connectLabel;
  final bool canConnect;
  final VoidCallback? onConnect;
  final VoidCallback? onPrevious;
  final VoidCallback? onNext;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: OutlinedButton(
            onPressed: onPrevious,
            child: Text(AppStrings.previousServer),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: _ConnectButton(
            label: connectLabel,
            enabled: canConnect,
            onPressed: onConnect,
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: OutlinedButton(
            onPressed: onNext,
            child: Text(AppStrings.nextServer),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Semantics(
            button: true,
            label: AppStrings.scanToggle,
            toggled: scanActive,
            value: scanActive ? A11yStrings.scanStateOn : A11yStrings.scanStateOff,
            excludeSemantics: true,
            child: ToggleButtons(
              isSelected: [scanActive],
              onPressed: (_) => onToggleScan(),
              constraints: const BoxConstraints(
                minWidth: MinTouchTarget.minSize,
                minHeight: MinTouchTarget.minSize,
              ),
              children: [
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  child: Text(
                    scanActive ? AppStrings.scanning : AppStrings.scanToggle,
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

class _ConnectButton extends StatelessWidget {
  const _ConnectButton({
    required this.label,
    required this.enabled,
    required this.onPressed,
  });

  final String label;
  final bool enabled;
  final VoidCallback? onPressed;

  @override
  Widget build(BuildContext context) {
    final button = FilledButton(
      onPressed: onPressed,
      child: Text(label),
    );
    if (enabled) {
      return button;
    }
    return Semantics(
      button: true,
      enabled: false,
      label: label,
      hint: A11yStrings.connectUnavailableHint,
      excludeSemantics: true,
      child: button,
    );
  }
}

class _ConnectionDetailsForm extends StatelessWidget {
  const _ConnectionDetailsForm({
    required this.nameCtrl,
    required this.hostCtrl,
    required this.portCtrl,
    required this.channelCtrl,
  });

  final TextEditingController nameCtrl;
  final TextEditingController hostCtrl;
  final TextEditingController portCtrl;
  final TextEditingController channelCtrl;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _LabeledField(
          label: AppStrings.serverNameLabel,
          helper: AppStrings.serverNameHint,
          controller: nameCtrl,
        ),
        const SizedBox(height: 8),
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              flex: 3,
              child: _LabeledField(
                label: AppStrings.serverHostLabel,
                helper: AppStrings.serverHostHint,
                controller: hostCtrl,
                textInputAction: TextInputAction.next,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              flex: 2,
              child: _LabeledField(
                label: AppStrings.portLabel,
                controller: portCtrl,
                keyboardType: TextInputType.number,
                textInputAction: TextInputAction.next,
              ),
            ),
          ],
        ),
        _LabeledField(
          label: AppStrings.channelLabel,
          helper: AppStrings.channelHint,
          controller: channelCtrl,
          textInputAction: TextInputAction.done,
        ),
      ],
    );
  }
}

class _LabeledField extends StatelessWidget {
  const _LabeledField({
    required this.label,
    required this.controller,
    this.helper,
    this.keyboardType,
    this.textInputAction,
  });

  final String label;
  final String? helper;
  final TextEditingController controller;
  final TextInputType? keyboardType;
  final TextInputAction? textInputAction;

  @override
  Widget build(BuildContext context) {
    // labelText only — avoid labelText+hintText in semantics (TalkBack braille
    // setText may concatenate them; flutter/flutter#113457).
    return TextField(
      controller: controller,
      keyboardType: keyboardType,
      textInputAction: textInputAction,
      decoration: InputDecoration(
        labelText: label,
        helperText: helper,
        helperMaxLines: 2,
      ),
    );
  }
}

class _RxVolumeSection extends StatefulWidget {
  const _RxVolumeSection({required this.percent, required this.onChanged});

  final int percent;
  final ValueChanged<int> onChanged;

  @override
  State<_RxVolumeSection> createState() => _RxVolumeSectionState();
}

class _RxVolumeSectionState extends State<_RxVolumeSection> {
  bool _a11yFocused = false;

  void _onDidGainAccessibilityFocus() {
    setState(() => _a11yFocused = true);
  }

  void _onDidLoseAccessibilityFocus() {
    setState(() => _a11yFocused = false);
  }

  void _onChangeEnd(double value) {
    final percent = value.round();
    A11yAnnounce.whenFocused(
      context,
      focused: _a11yFocused,
      message: AppStrings.rxVolumePercentAccessibility(percent),
    );
  }

  @override
  Widget build(BuildContext context) {
    final percent = widget.percent;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        ExcludeSemantics(
          child: Row(
            children: [
              Expanded(
                child: Text(
                  AppStrings.rxVolumeLabel,
                  style: Theme.of(context).textTheme.bodyMedium,
                ),
              ),
              Text(
                AppStrings.rxVolumePercent(percent),
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      fontWeight: FontWeight.bold,
                    ),
              ),
            ],
          ),
        ),
        Semantics(
          slider: true,
          label:
              '${AppStrings.rxVolumeLabel} ${AppStrings.rxVolumePercentAccessibility(percent)}',
          value: AppStrings.rxVolumePercent(percent),
          increasedValue: percent < 200
              ? AppStrings.rxVolumePercent(percent + 1)
              : null,
          decreasedValue: percent > 0
              ? AppStrings.rxVolumePercent(percent - 1)
              : null,
          onDidGainAccessibilityFocus: _onDidGainAccessibilityFocus,
          onDidLoseAccessibilityFocus: _onDidLoseAccessibilityFocus,
          child: Slider(
            value: percent.toDouble(),
            min: 0,
            max: 200,
            divisions: 200,
            onChanged: (v) => widget.onChanged(v.round()),
            onChangeEnd: _onChangeEnd,
          ),
        ),
      ],
    );
  }
}

class _PttArea extends StatelessWidget {
  const _PttArea({
    required this.enabled,
    required this.active,
    required this.label,
    required this.locked,
    required this.pttLockSec,
    required this.sessionConnected,
    required this.onPttDown,
    required this.onPttUp,
    required this.onCall,
  });

  final bool enabled;
  final bool active;
  final String label;
  final bool locked;
  final int pttLockSec;
  final bool sessionConnected;
  final VoidCallback onPttDown;
  final VoidCallback onPttUp;
  final VoidCallback onCall;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 220,
      child: Stack(
        alignment: Alignment.center,
        children: [
          PttGestureButton(
            enabled: enabled,
            active: active,
            locked: locked,
            pttLockSec: pttLockSec,
            sessionConnected: sessionConnected,
            onPttDown: onPttDown,
            onPttUp: onPttUp,
            child: Container(
              width: 190,
              height: 190,
              alignment: Alignment.center,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: active
                    ? Theme.of(context).colorScheme.primaryContainer
                    : Theme.of(context).colorScheme.primary,
              ),
              child: Text(
                label,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      color: Theme.of(context).colorScheme.onPrimary,
                      fontWeight: FontWeight.bold,
                    ),
              ),
            ),
          ),
          Positioned(
            right: 0,
            bottom: 0,
            child: SizedBox(
              width: 96,
              height: 72,
              child: OutlinedButton(
                onPressed: enabled && !active && !locked ? onCall : null,
                child: Text(AppStrings.callSignal),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
