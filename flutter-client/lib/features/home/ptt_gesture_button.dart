import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';

import '../../a11y/a11y.dart';
import '../../l10n/a11y_strings.dart';
import 'ptt_touch_policy.dart';

/// On-screen PTT: tap toggles TX latch; hold = classic push-to-talk.
///
/// Keyboard Space toggles (same as media-button policy). Screen reader uses
/// short state descriptions (Kotlin parity) plus Start/Stop actions.
class PttGestureButton extends StatefulWidget {
  const PttGestureButton({
    super.key,
    required this.enabled,
    required this.active,
    required this.locked,
    required this.pttLockSec,
    required this.onPttDown,
    required this.onPttUp,
    required this.child,
  });

  final bool enabled;
  final bool active;
  final bool locked;
  final int pttLockSec;
  final VoidCallback onPttDown;
  final VoidCallback onPttUp;
  final Widget child;

  @override
  State<PttGestureButton> createState() => _PttGestureButtonState();
}

class _PttGestureButtonState extends State<PttGestureButton> {
  bool _pointerDown = false;
  bool _holdMode = false;
  Timer? _holdTimer;

  @override
  void dispose() {
    _holdTimer?.cancel();
    super.dispose();
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

  String _semanticsLabel() {
    if (!widget.enabled) {
      if (widget.locked && widget.pttLockSec > 0) {
        return A11yStrings.pttCountdown(widget.pttLockSec);
      }
      if (widget.locked) {
        return A11yStrings.pttLocked;
      }
      return A11yStrings.pttUnavailable;
    }
    if (widget.active) {
      return A11yStrings.pttToggleHint;
    }
    return A11yStrings.pttHoldHint;
  }

  @override
  Widget build(BuildContext context) {
    return Focus(
      child: Shortcuts(
        shortcuts: {
          const SingleActivator(LogicalKeyboardKey.space): const _PttKeyboardIntent(),
        },
        child: Actions(
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
            excludeSemantics: true,
            customSemanticsActions: widget.enabled
                ? {
                    CustomSemanticsAction(label: A11yStrings.pttStartAction):
                        widget.onPttDown,
                    CustomSemanticsAction(label: A11yStrings.pttStopAction):
                        widget.onPttUp,
                  }
                : const {},
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTapDown: widget.enabled ? _onTapDown : null,
              onTapUp: widget.enabled ? _onTapUp : null,
              onTapCancel: widget.enabled ? _onTapCancel : null,
              child: MinTouchTarget(child: widget.child),
            ),
          ),
        ),
      ),
    );
  }
}

class _PttKeyboardIntent extends Intent {
  const _PttKeyboardIntent();
}
