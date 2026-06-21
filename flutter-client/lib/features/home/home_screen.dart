import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../a11y/a11y.dart';
import '../../domain/scan_mode.dart';
import '../../domain/server_profile.dart';
import '../../a11y/a11y_slider_field.dart';
import '../../a11y/a11y_desktop_numeric_field.dart';
import '../../platform/native_platform.dart';
import '../../platform/windows/desktop_shell.dart';
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
        if (next != null && next != previous && mounted) {
          A11yAnnounce.confirmation(context, next);
          ref
              .read(homeScreenControllerProvider.notifier)
              .clearStatusMessage();
        }
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
    final connectionDetailsExpanded = ref.watch(
      homeScreenControllerProvider.select((s) => s.connectionDetailsExpanded),
    );
    final controller = ref.read(homeScreenControllerProvider.notifier);

    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, result) {
        if (didPop) {
          return;
        }
        unawaited(controller.handleSystemBack());
      },
      child: Scaffold(
      body: SafeArea(
        child: FocusTraversalGroup(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (connectionDetailsExpanded) ...[
                Expanded(
                  child: SingleChildScrollView(
                    padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
                    keyboardDismissBehavior:
                        ScrollViewKeyboardDismissBehavior.onDrag,
                    child: _buildConnectionScrollContent(),
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  child: _PttAreaConsumer(
                    expanded: false,
                    onPttDown: controller.pttDown,
                    onPttUp: controller.pttUp,
                    onCall: controller.sendCall,
                  ),
                ),
              ] else
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Flexible(
                        fit: FlexFit.loose,
                        child: SingleChildScrollView(
                          padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
                          keyboardDismissBehavior:
                              ScrollViewKeyboardDismissBehavior.onDrag,
                          child: _buildConnectionScrollContent(),
                        ),
                      ),
                      Expanded(
                        child: Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 16),
                          child: _PttAreaConsumer(
                            expanded: true,
                            onPttDown: controller.pttDown,
                            onPttUp: controller.pttUp,
                            onCall: controller.sendCall,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              const _FooterVersion(),
            ],
          ),
        ),
      ),
    ),
    );
  }

  Widget _buildConnectionScrollContent() {
    final controller = ref.read(homeScreenControllerProvider.notifier);
    final repeaterEnabled = ref.read(
      homeScreenControllerProvider.select((s) => s.draftProfile.repeater),
    );
    final connectionDetailsExpanded = ref.read(
      homeScreenControllerProvider.select((s) => s.connectionDetailsExpanded),
    );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _HeaderRow(
          repeaterEnabled: repeaterEnabled,
          onRepeaterToggled: controller.setRepeaterMode,
          onExit: () async {
            await controller.shutdownForAppExit();
            if (NativePlatform.isAndroid) {
              await NativePlatform.requestAppExit();
            } else if (NativePlatform.isWindows) {
              await ref.read(desktopShellProvider).exitApplication();
            }
          },
        ),
        const SizedBox(height: 8),
        const _StatusChips(),
        const _ErrorStatusArea(),
        const SizedBox(height: 12),
        _ServerProfileArea(
          onScanPressed: () => _onScanPressed(ref.read(homeScreenControllerProvider).scanActive),
        ),
        if (connectionDetailsExpanded)
          _ExpandedFormActions(
            nameCtrl: _nameCtrl,
            hostCtrl: _hostCtrl,
            portCtrl: _portCtrl,
            channelCtrl: _channelCtrl,
            onSave: () async {
              _syncProfile();
              await controller.saveCurrentProfile();
              _loadControllersFromProfile(
                ref.read(homeScreenControllerProvider).profile,
              );
            },
            onDelete: () {
              _syncProfile();
              controller.deleteCurrentProfile();
            },
            onShare: _shareConnection,
            onImport: _importConnection,
            onConnect: () {
              _syncProfile();
              controller.toggleConnection();
            },
          ),
        const SizedBox(height: 24),
        if (NativePlatform.isDesktop)
          A11yDesktopNumericField(
            value: ref.read(homeScreenControllerProvider).rxVolumePercent,
            min: 0,
            max: 200,
            step: 5,
            title: AppStrings.rxVolumeLabel,
            suffix: '%',
            maxDigits: 3,
            onChanged: controller.setRxVolume,
            onCommit: controller.finishRxVolumePreview,
          )
        else
          A11ySliderField(
            value: ref.read(homeScreenControllerProvider).rxVolumePercent.toDouble(),
            min: 0,
            max: 200,
            divisions: 200,
            semanticStep: 5,
            title: AppStrings.rxVolumeLabel,
            semanticsLabel: AppStrings.rxVolumeLabel,
            semanticsValue: AppStrings.rxVolumePercentAccessibility(ref.read(homeScreenControllerProvider).rxVolumePercent),
            formatStepValue: (value) => AppStrings.rxVolumePercent(value.round()),
            onChanged: (value) => controller.setRxVolume(value.round()),
            onChangeEnd: (value) =>
                controller.finishRxVolumePreview(value.round()),
            announceOnChangeEnd: (value) =>
                AppStrings.rxVolumePercentAccessibility(value.round()),
          ),
        const SizedBox(height: 16),
      ],
    );
  }
}

class _HeaderRow extends StatefulWidget {
  const _HeaderRow({
    required this.repeaterEnabled,
    required this.onRepeaterToggled,
    required this.onExit,
  });

  final bool repeaterEnabled;
  final ValueChanged<bool> onRepeaterToggled;
  final VoidCallback onExit;

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
        const PopupMenuDivider(),
        PopupMenuItem(
          value: 'exit',
          child: Text(AppStrings.menuExit),
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
      case 'exit':
        widget.onExit();
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

class _StatusChips extends ConsumerWidget {
  const _StatusChips();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connection = ref.watch(
      homeScreenControllerProvider.select((s) => s.connectionDisplayChip),
    );
    final signal = ref.watch(
      homeScreenControllerProvider.select((s) => s.signalChip),
    );
    final chipStyle = Theme.of(context).textTheme.bodyMedium?.copyWith(
          fontWeight: FontWeight.bold,
        );
    return Row(
      children: [
        Expanded(
          child: A11yLiveStatusChip(
            label: connection,
            child: Text(connection, style: chipStyle),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: A11yLiveStatusChip(
            label: signal,
            alignment: Alignment.centerRight,
            child: Text(signal, style: chipStyle, textAlign: TextAlign.end),
          ),
        ),
      ],
    );
  }
}

class _ErrorStatusArea extends ConsumerWidget {
  const _ErrorStatusArea();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final statusInfo = ref.watch(
      homeScreenControllerProvider.select((s) => s.statusInfo),
    );
    final lastError = ref.watch(
      homeScreenControllerProvider.select((s) => s.lastError),
    );

    if (statusInfo == null && lastError == null) {
      return const SizedBox.shrink();
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (statusInfo != null) ...[
          const SizedBox(height: 8),
          Text(
            statusInfo,
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
        if (lastError != null) ...[
          const SizedBox(height: 8),
          Semantics(
            liveRegion: true,
            child: Text(
              lastError,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Theme.of(context).colorScheme.error,
                  ),
            ),
          ),
        ],
      ],
    );
  }
}

class _ServerProfileArea extends ConsumerWidget {
  const _ServerProfileArea({required this.onScanPressed});

  final VoidCallback onScanPressed;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profiles = ref.watch(
      homeScreenControllerProvider.select((s) => s.profiles),
    );
    final selectedIndex = ref.watch(
      homeScreenControllerProvider.select((s) => s.selectedServerIndex),
    );
    final expanded = ref.watch(
      homeScreenControllerProvider.select((s) => s.connectionDetailsExpanded),
    );
    final canSelect = ref.watch(
      homeScreenControllerProvider.select((s) => s.canSelectProfiles),
    );
    final controller = ref.read(homeScreenControllerProvider.notifier);

    final canNavigate = ref.watch(
      homeScreenControllerProvider.select((s) => s.canNavigateProfiles),
    );
    final hasPrevious = ref.watch(
      homeScreenControllerProvider.select((s) => s.hasPreviousProfile),
    );
    final hasNext = ref.watch(
      homeScreenControllerProvider.select((s) => s.hasNextProfile),
    );
    final isConnected = ref.watch(
      homeScreenControllerProvider.select((s) => s.isConnected),
    );
    final isConnecting = ref.watch(
      homeScreenControllerProvider.select((s) => s.isConnecting),
    );
    final sessionSupported = ref.watch(
      homeScreenControllerProvider.select((s) => s.sessionSupported),
    );
    final scanActive = ref.watch(
      homeScreenControllerProvider.select((s) => s.scanActive),
    );

    final connectLabel = isConnected || isConnecting
        ? AppStrings.disconnectServer
        : AppStrings.connectServer;
    final canConnect = sessionSupported;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
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
          initialValue: selectedIndex.clamp(0, profiles.length - 1),
          decoration: InputDecoration(
            labelText: AppStrings.serverProfiles,
            contentPadding:
                const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          ),
          items: [
            for (var i = 0; i < profiles.length; i++)
              DropdownMenuItem(
                value: i,
                child: Text(
                  profiles[i].name.isEmpty
                      ? AppStrings.profileNumberFallback(i + 1)
                      : profiles[i].name,
                ),
              ),
          ],
          onChanged: canSelect
              ? (index) {
                  if (index != null) {
                    controller.selectProfile(index);
                  }
                }
              : null,
        ),
        const SizedBox(height: 8),
        OutlinedButton(
          onPressed: controller.toggleConnectionDetails,
          child: Text(
            expanded
                ? AppStrings.collapseConnectionDetails
                : AppStrings.expandConnectionDetails,
          ),
        ),
        if (!expanded) ...[
          const SizedBox(height: 8),
          _CollapsedActions(
            scanActive: scanActive,
            onToggleScan: onScanPressed,
            connectLabel: connectLabel,
            canConnect: canConnect,
            onConnect: canConnect
                ? () {
                    unawaited(controller.connectToSelectedProfile());
                  }
                : null,
            onPrevious: canNavigate ? controller.previousProfile : null,
            hasPrevious: hasPrevious,
            onNext: canNavigate ? controller.nextProfile : null,
            hasNext: hasNext,
          ),
        ],
      ],
    );
  }
}

class _ExpandedFormActions extends ConsumerWidget {
  const _ExpandedFormActions({
    required this.nameCtrl,
    required this.hostCtrl,
    required this.portCtrl,
    required this.channelCtrl,
    required this.onSave,
    required this.onDelete,
    required this.onShare,
    required this.onImport,
    required this.onConnect,
  });

  final TextEditingController nameCtrl;
  final TextEditingController hostCtrl;
  final TextEditingController portCtrl;
  final TextEditingController channelCtrl;
  final VoidCallback onSave;
  final VoidCallback onDelete;
  final VoidCallback onShare;
  final VoidCallback onImport;
  final VoidCallback onConnect;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profiles = ref.watch(
      homeScreenControllerProvider.select((s) => s.profiles),
    );
    final canMoveUp = ref.watch(
      homeScreenControllerProvider.select((s) => s.canMoveProfileUp),
    );
    final canMoveDown = ref.watch(
      homeScreenControllerProvider.select((s) => s.canMoveProfileDown),
    );
    final isConnected = ref.watch(
      homeScreenControllerProvider.select((s) => s.isConnected),
    );
    final isConnecting = ref.watch(
      homeScreenControllerProvider.select((s) => s.isConnecting),
    );
    final sessionSupported = ref.watch(
      homeScreenControllerProvider.select((s) => s.sessionSupported),
    );
    final controller = ref.read(homeScreenControllerProvider.notifier);

    final connectLabel = isConnected || isConnecting
        ? AppStrings.disconnectServer
        : AppStrings.connectServer;
    final canConnect = sessionSupported;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SizedBox(height: 8),
        _ConnectionDetailsForm(
          nameCtrl: nameCtrl,
          hostCtrl: hostCtrl,
          portCtrl: portCtrl,
          channelCtrl: channelCtrl,
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Expanded(
              child: OutlinedButton(
                onPressed: onSave,
                child: Text(AppStrings.saveServer),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: OutlinedButton(
                onPressed: profiles.length > 1 ? onDelete : null,
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
                onPressed: canMoveUp ? controller.moveProfileUp : null,
                child: Text(AppStrings.moveServerUp),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: OutlinedButton(
                onPressed: canMoveDown ? controller.moveProfileDown : null,
                child: Text(AppStrings.moveServerDown),
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Expanded(
              child: OutlinedButton(
                onPressed: onShare,
                child: Text(AppStrings.shareConnection),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: OutlinedButton(
                onPressed: onImport,
                child: Text(AppStrings.importConnection),
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        _ConnectButton(
          label: connectLabel,
          enabled: canConnect,
          onPressed: canConnect ? onConnect : null,
        ),
      ],
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

class _FooterVersion extends ConsumerWidget {
  const _FooterVersion();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final coreVersion = ref.watch(
      homeScreenControllerProvider.select((s) => s.coreVersion),
    );
    final protocolVersion = ref.watch(
      homeScreenControllerProvider.select((s) => s.protocolVersion),
    );
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
      child: ExcludeSemantics(
        child: Text(
          '${AppStrings.coreVersionFooter}: $coreVersion · '
          '${AppStrings.protocolLabel} $protocolVersion',
          textAlign: TextAlign.center,
          style: Theme.of(context).textTheme.bodySmall,
        ),
      ),
    );
  }
}

class _PttAreaConsumer extends ConsumerWidget {
  const _PttAreaConsumer({
    required this.expanded,
    required this.onPttDown,
    required this.onPttUp,
    required this.onCall,
  });

  final bool expanded;
  final VoidCallback onPttDown;
  final VoidCallback onPttUp;
  final VoidCallback onCall;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(homeScreenControllerProvider.select((s) => (
          txActive: s.txActive,
          pttServerLocked: s.pttServerLocked,
          pttLockSec: s.pttLockSec,
          txCountdownSec: s.txCountdownSec,
          isConnected: s.isConnected,
        )));
    final pttUiEnabled = pttUiEnabledFor(ref.read(homeScreenControllerProvider));
    final label = pttButtonLabel(
      pttUiEnabled: pttUiEnabled,
      txActive: state.txActive,
      pttServerLocked: state.pttServerLocked,
      pttLockSec: state.pttLockSec,
      txCountdownSec: state.txCountdownSec,
    );
    return _PttArea(
      enabled: pttUiEnabled,
      active: state.txActive,
      label: label,
      locked: state.pttServerLocked,
      pttLockSec: state.pttLockSec,
      sessionConnected: state.isConnected,
      expanded: expanded,
      onPttDown: onPttDown,
      onPttUp: onPttUp,
      onCall: onCall,
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
    required this.expanded,
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
  final bool expanded;
  final VoidCallback onPttDown;
  final VoidCallback onPttUp;
  final VoidCallback onCall;

  static const _compactHeight = 220.0;
  static const _minCircleSize = 190.0;
  static const _maxCircleSize = 320.0;

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

    Widget buildArea(double height) {
      final circleSize = widget.expanded
          ? (height * 0.72).clamp(_PttArea._minCircleSize, _PttArea._maxCircleSize)
          : _PttArea._minCircleSize;

      return SizedBox(
        height: height,
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
                  width: circleSize,
                  height: circleSize,
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
                    onPressed:
                        widget.enabled && !widget.active ? widget.onCall : null,
                    child: Text(AppStrings.callSignal),
                  ),
                ),
              ),
            ),
          ],
        ),
      );
    }

    if (widget.expanded) {
      return LayoutBuilder(
        builder: (context, constraints) =>
            buildArea(constraints.maxHeight),
      );
    }

    return buildArea(_PttArea._compactHeight);
  }
}
