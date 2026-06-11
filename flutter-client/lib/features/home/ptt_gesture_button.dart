import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';

import '../../a11y/a11y.dart';
import '../../a11y/ptt_a11y.dart';
import '../../l10n/a11y_strings.dart';
import 'ptt_touch_policy.dart';

/// On-screen PTT: tap toggles TX latch; hold = classic push-to-talk.
///
/// Screen reader: short state labels (Kotlin parity), Start/Stop actions,
/// lock/unlock announce only while a11y focus is here; countdown seconds
/// read on focus, not re-announced every tick.
class PttGestureButton extends StatefulWidget {
  const PttGestureButton({
    super.key,
    required this.enabled,
    required this.active,
    required this.locked,
    required this.pttLockSec,
    required this.sessionConnected,
    required this.onPttDown,
    required this.onPttUp,
    required this.child,
  });

  final bool enabled;
  final bool active;
  final bool locked;
  final int pttLockSec;
  final bool sessionConnected;
  final VoidCallback onPttDown;
  final VoidCallback onPttUp;
  final Widget child;

  @override
  State<PttGestureButton> createState() => _PttGestureButtonState();
}

class _PttGestureButtonState extends State<PttGestureButton> {
  bool _pointerDown = false;
  bool _holdMode = false;
  bool _a11yFocused = false;
  String? _frozenCountdownLabel;
  Timer? _holdTimer;

  @override
  void dispose() {
    _holdTimer?.cancel();
    super.dispose();
  }

  @override
  void didUpdateWidget(covariant PttGestureButton oldWidget) {
    super.didUpdateWidget(oldWidget);
    _maybeAnnounceServerPttLockChange(oldWidget);
  }

  void _maybeAnnounceServerPttLockChange(PttGestureButton oldWidget) {
    if (!widget.sessionConnected) {
      return;
    }
    final wasLocked = oldWidget.locked && !oldWidget.active;
    final isLocked = widget.locked && !widget.active;
    if (wasLocked == isLocked) {
      return;
    }
    A11yAnnounce.whenFocused(
      context,
      focused: _a11yFocused,
      message: isLocked
          ? A11yStrings.pttLockedAnnouncement
          : A11yStrings.pttUnlockedAnnouncement,
    );
  }

  void _cancelHoldTimer() {
    _holdTimer?.cancel();
    _holdTimer = null;
  }

  void _onTapDown(TapDownDetails details) {
    if (!widget.enabled) {
      return;
    }
    _pointerDown = true;
    _holdMode = false;
    _cancelHoldTimer();
    _holdTimer = Timer(
      const Duration(milliseconds: PttTouchPolicy.holdThresholdMs),
      () {
        if (!_pointerDown || !mounted) {
          return;
        }
        setState(() => _holdMode = true);
        if (!widget.active) {
          widget.onPttDown();
        }
      },
    );
  }

  void _onTapUp(TapUpDetails details) {
    _finishPointer(upAction: _resolveUpAction());
  }

  void _onTapCancel() {
    _finishPointer(
      upAction: _holdMode ? PttPointerUpAction.releaseHold : PttPointerUpAction.none,
    );
  }

  PttPointerUpAction _resolveUpAction() {
    if (!_pointerDown) {
      return PttPointerUpAction.none;
    }
    return pttPointerUpAction(holdMode: _holdMode, txActive: widget.active);
  }

  void _finishPointer({required PttPointerUpAction upAction}) {
    _cancelHoldTimer();
    _pointerDown = false;
    final wasHold = _holdMode;
    _holdMode = false;
    if (!widget.enabled) {
      return;
    }
    switch (upAction) {
      case PttPointerUpAction.toggleOn:
        widget.onPttDown();
      case PttPointerUpAction.toggleOff:
      case PttPointerUpAction.releaseHold:
        widget.onPttUp();
      case PttPointerUpAction.none:
        break;
    }
    if (wasHold && upAction == PttPointerUpAction.none) {
      widget.onPttUp();
    }
  }

  void _onKeyboardToggle() {
    if (!widget.enabled) {
      return;
    }
    if (widget.active) {
      widget.onPttUp();
    } else {
      widget.onPttDown();
    }
  }

  PttA11yBucket _bucket() {
    return resolvePttA11yBucket(
      enabled: widget.enabled,
      active: widget.active,
      serverPttLocked: widget.locked,
      pttLockSec: widget.pttLockSec,
    );
  }

  String _semanticsLabel() {
    return pttA11yLabelFor(
      bucket: _bucket(),
      pttLockSec: widget.pttLockSec,
      frozenCountdownLabel:
          _a11yFocused ? _frozenCountdownLabel : null,
    );
  }

  void _onDidGainAccessibilityFocus() {
    final bucket = _bucket();
    setState(() {
      _a11yFocused = true;
      if (bucket == PttA11yBucket.countdown) {
        _frozenCountdownLabel = A11yStrings.pttCountdown(widget.pttLockSec);
      }
    });
  }

  void _onDidLoseAccessibilityFocus() {
    setState(() {
      _a11yFocused = false;
      _frozenCountdownLabel = null;
    });
  }

  @override
  Widget build(BuildContext context) {
    return FocusableActionDetector(
      enabled: widget.enabled,
      mouseCursor: widget.enabled
          ? SystemMouseCursors.click
          : SystemMouseCursors.basic,
      shortcuts: const {
        SingleActivator(LogicalKeyboardKey.space): _PttKeyboardIntent(),
        SingleActivator(LogicalKeyboardKey.enter): _PttKeyboardIntent(),
        SingleActivator(LogicalKeyboardKey.numpadEnter): _PttKeyboardIntent(),
      },
      actions: {
        _PttKeyboardIntent: CallbackAction<_PttKeyboardIntent>(
          onInvoke: (_) {
            _onKeyboardToggle();
            return null;
          },
        ),
      },
      child: Semantics(
        button: true,
        enabled: widget.enabled,
        label: _semanticsLabel(),
        onDidGainAccessibilityFocus: _onDidGainAccessibilityFocus,
        onDidLoseAccessibilityFocus: _onDidLoseAccessibilityFocus,
        customSemanticsActions: widget.enabled
            ? widget.active
                ? {
                    CustomSemanticsAction(label: A11yStrings.pttStopAction):
                        widget.onPttUp,
                  }
                : {
                    CustomSemanticsAction(label: A11yStrings.pttStartAction):
                        widget.onPttDown,
                  }
            : const {},
        child: ExcludeSemantics(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTapDown: widget.enabled ? _onTapDown : null,
            onTapUp: widget.enabled ? _onTapUp : null,
            onTapCancel: widget.enabled ? _onTapCancel : null,
            child: MinTouchTarget(child: widget.child),
          ),
        ),
      ),
    );
  }
}

class _PttKeyboardIntent extends Intent {
  const _PttKeyboardIntent();
}
