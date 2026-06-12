import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../a11y/a11y.dart';
import '../../domain/scan_mode.dart';
import '../../domain/server_profile.dart';
import '../../a11y/a11y_slider_field.dart';
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
    final pttUiEnabled = pttUiEnabledFor(state);

    return Scaffold(
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
                      onPrevious: state.canNavigateProfiles
                          ? controller.previousProfile
                          : null,
                      hasPrevious: state.hasPreviousProfile,
                      onNext: state.canNavigateProfiles
                          ? controller.nextProfile
                          : null,
                      hasNext: state.hasNextProfile,
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
                  A11ySliderField(
                    value: state.rxVolumePercent.toDouble(),
                    min: 0,
                    max: 200,
                    divisions: 200,
                    title: AppStrings.rxVolumeLabel,
                    displayValue:
                        AppStrings.rxVolumePercent(state.rxVolumePercent),
                    semanticsLabel:
                        '${AppStrings.rxVolumeLabel} ${AppStrings.rxVolumePercentAccessibility(state.rxVolumePercent)}',
                    semanticsValue:
                        AppStrings.rxVolumePercent(state.rxVolumePercent),
                    increasedValue: state.rxVolumePercent < 200
                        ? AppStrings.rxVolumePercent(state.rxVolumePercent + 1)
                        : null,
                    decreasedValue: state.rxVolumePercent > 0
                        ? AppStrings.rxVolumePercent(state.rxVolumePercent - 1)
                        : null,
                    onChanged: (value) =>
                        controller.setRxVolume(value.round()),
                    onChangeEnd: (value) =>
                        controller.finishRxVolumePreview(value.round()),
                    announceOnChangeEnd: (value) =>
                        AppStrings.rxVolumePercentAccessibility(value.round()),
                  ),
                  const SizedBox(height: 16),
                  _PttArea(
                    enabled: pttUiEnabled,
                    active: state.txActive,
                    label: pttButtonLabel(
                      pttUiEnabled: pttUiEnabled,
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
    );
  }
}

class _HeaderRow extends StatefulWidget {
  const _HeaderRow({
    required this.repeaterEnabled,
    required this.onRepeaterToggled,
  });

  final bool repeaterEnabled;
  final ValueChanged<bool> onRepeaterToggled;

  @override
  State<_HeaderRow> createState() => _HeaderRowState();
}

class _HeaderRowState extends State<_HeaderRow> {
  final _moreButtonKey = GlobalKey();

  Future<void> _showMoreMenu() async {
    final buttonContext = _moreButtonKey.currentContext;
    if (buttonContext == null) {
      return;
    }

    final box = buttonContext.findRenderObject()! as RenderBox;
    final overlay =
        Overlay.of(buttonContext).context.findRenderObject()! as RenderBox;
    final position = RelativeRect.fromRect(
      Rect.fromPoints(
        box.localToGlobal(Offset.zero, ancestor: overlay),
        box.localToGlobal(box.size.bottomRight(Offset.zero), ancestor: overlay),
      ),
      Offset.zero & overlay.size,
    );

    final value = await showMenu<String>(
      context: buttonContext,
      position: position,
      popUpAnimationStyle: AnimationStyle.noAnimation,
      items: [
        CheckedPopupMenuItem(
          value: 'repeater',
          checked: widget.repeaterEnabled,
          child: Text(AppStrings.menuRepeaterMode),
        ),
        PopupMenuItem(
          value: 'settings',
          child: Text(AppStrings.menuSettings),
        ),
      ],
    );

    if (!mounted || value == null) {
      return;
    }

    switch (value) {
      case 'repeater':
        widget.onRepeaterToggled(!widget.repeaterEnabled);
      case 'settings':
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (mounted) {
            context.push('/settings');
          }
        });
    }
  }

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
        OutlinedButton(
          key: _moreButtonKey,
          onPressed: _showMoreMenu,
          child: Text(AppStrings.menuMore),
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
    required this.hasPrevious,
    required this.onNext,
    required this.hasNext,
  });

  final bool scanActive;
  final VoidCallback onToggleScan;
  final String connectLabel;
  final bool canConnect;
  final VoidCallback? onConnect;
  final VoidCallback? onPrevious;
  final bool hasPrevious;
  final VoidCallback? onNext;
  final bool hasNext;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: OutlinedButton(
            onPressed: onPrevious != null && hasPrevious ? onPrevious : null,
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
            onPressed: onNext != null && hasNext ? onNext : null,
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

class _PttArea extends StatefulWidget {
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
  State<_PttArea> createState() => _PttAreaState();
}

class _PttAreaState extends State<_PttArea> {
  bool _pttLatched = false;

  @override
  void didUpdateWidget(covariant _PttArea oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.active && !widget.active) {
      _pttLatched = false;
    }
  }

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final buttonColor = !widget.active
        ? scheme.primary
        : _pttLatched
            ? scheme.error
            : scheme.primaryContainer;
    final labelColor = !widget.active
        ? scheme.onPrimary
        : _pttLatched
            ? scheme.onError
            : scheme.onPrimaryContainer;

    return SizedBox(
      height: 220,
      child: Stack(
        alignment: Alignment.center,
        children: [
          Opacity(
            opacity: widget.enabled ? 1.0 : 0.5,
            child: PttGestureButton(
              enabled: widget.enabled,
              active: widget.active,
              locked: widget.locked,
              pttLockSec: widget.pttLockSec,
              sessionConnected: widget.sessionConnected,
              onPttDown: widget.onPttDown,
              onPttUp: widget.onPttUp,
              onLatchedChanged: (latched) {
                if (_pttLatched != latched) {
                  setState(() => _pttLatched = latched);
                }
              },
              child: Container(
                width: 190,
                height: 190,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: buttonColor,
                ),
                child: Text(
                  widget.label,
                  textAlign: TextAlign.center,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        color: labelColor,
                        fontWeight: FontWeight.bold,
                      ),
                ),
              ),
            ),
          ),
          Positioned(
            right: 0,
            bottom: 0,
            child: Opacity(
              opacity: widget.enabled && !widget.active ? 1.0 : 0.5,
              child: SizedBox(
                width: 96,
                height: 72,
                child: OutlinedButton(
                  onPressed: widget.enabled && !widget.active ? widget.onCall : null,
                  child: Text(AppStrings.callSignal),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
