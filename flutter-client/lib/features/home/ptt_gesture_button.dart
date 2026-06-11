import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';

import '../../a11y/a11y.dart';
import '../../a11y/ptt_a11y.dart';
import '../../l10n/a11y_strings.dart';
import 'ptt_touch_policy.dart';

/// On-screen PTT: hold to talk; swipe up while holding to latch until tap.
///
/// Keyboard Space/Enter toggles latched transmit. Screen reader uses explicit
/// Start/Stop actions; server busy lock announcements stay focus-scoped.
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
    required this.onLatchedChanged,
    required this.child,
  });

  final bool enabled;
  final bool active;
  final bool locked;
  final int pttLockSec;
  final bool sessionConnected;
  final VoidCallback onPttDown;
  final VoidCallback onPttUp;
  final ValueChanged<bool> onLatchedChanged;
  final Widget child;

  @override
  State<PttGestureButton> createState() => _PttGestureButtonState();
}

class _PttGestureButtonState extends State<PttGestureButton> {
  bool _pointerDown = false;
  bool _latched = false;
  bool _tapToReleasePending = false;
  bool _a11yFocused = false;
  String? _frozenCountdownLabel;
  double _pointerStartY = 0;

  @override
  void didUpdateWidget(covariant PttGestureButton oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.active && !widget.active) {
      _setLatched(false);
    }
    _maybeAnnounceServerPttLockChange(oldWidget);
  }

  void _setLatched(bool value) {
    if (_latched == value) {
      return;
    }
    _latched = value;
    widget.onLatchedChanged(value);
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

  void _onPointerDown(PointerDownEvent event) {
    if (!widget.enabled) {
      return;
    }
    if (widget.active && _latched) {
      _tapToReleasePending = true;
      return;
    }
    _pointerDown = true;
    _tapToReleasePending = false;
    _pointerStartY = event.localPosition.dy;
    widget.onPttDown();
  }

  void _onPointerMove(PointerMoveEvent event) {
    if (!_pointerDown || !widget.enabled || _latched) {
      return;
    }
    if (!widget.active) {
      return;
    }
    if (pttSwipeUpLockTriggered(
      startY: _pointerStartY,
      currentY: event.localPosition.dy,
    )) {
      setState(() => _setLatched(true));
    }
  }

  void _onPointerUp(PointerUpEvent event) {
    _finishPointer();
  }

  void _onPointerCancel(PointerCancelEvent event) {
    _finishPointer();
  }

  void _finishPointer() {
    if (!widget.enabled) {
      _pointerDown = false;
      _tapToReleasePending = false;
      return;
    }
    final action = pttPointerUpAction(
      latched: _latched,
      tapToReleasePending: _tapToReleasePending,
    );
    _pointerDown = false;
    _tapToReleasePending = false;
    switch (action) {
      case PttPointerUpAction.releaseLatched:
        _setLatched(false);
        widget.onPttUp();
      case PttPointerUpAction.releaseTransmit:
        widget.onPttUp();
      case PttPointerUpAction.none:
        break;
    }
  }

  void _onKeyboardToggle() {
    if (!widget.enabled) {
      return;
    }
    if (widget.active) {
      _setLatched(false);
      widget.onPttUp();
    } else {
      widget.onPttDown();
      _setLatched(true);
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
                        () {
                      _setLatched(false);
                      widget.onPttUp();
                    },
                  }
                : {
                    CustomSemanticsAction(label: A11yStrings.pttStartAction):
                        () {
                      widget.onPttDown();
                      _setLatched(true);
                    },
                  }
            : const {},
        child: ExcludeSemantics(
          child: Listener(
            behavior: HitTestBehavior.opaque,
            onPointerDown: widget.enabled ? _onPointerDown : null,
            onPointerMove: widget.enabled ? _onPointerMove : null,
            onPointerUp: widget.enabled ? _onPointerUp : null,
            onPointerCancel: widget.enabled ? _onPointerCancel : null,
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
