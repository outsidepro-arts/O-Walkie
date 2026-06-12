import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';

import '../l10n/a11y_strings.dart';

// Status chips: use [A11yLiveStatusChip] — liveRegion toggles on
// onDidGainAccessibilityFocus / onDidLoseAccessibilityFocus so updates are
// spoken only while TalkBack/Narrator focus is on that chip.
//
// For Kotlin showConfirmation (Toast + announceForAccessibility), use
// [A11yAnnounce.confirmation].

/// Kotlin [View.announceForAccessibility] parity for Flutter.
abstract final class A11yAnnounce {
  static Future<void> whenSupported(
    BuildContext context,
    String message,
  ) async {
    if (!context.mounted) {
      return;
    }
    if (!MediaQuery.supportsAnnounceOf(context)) {
      return;
    }
    await SemanticsService.sendAnnouncement(
      View.of(context),
      message,
      Directionality.of(context),
    );
  }

  /// Speaks [message] only when [focused] is true (TalkBack focus on this node).
  static void whenFocused(
    BuildContext context, {
    required bool focused,
    required String message,
  }) {
    if (!focused) {
      return;
    }
    unawaited(whenSupported(context, message));
  }

  /// Kotlin [MainActivity.showConfirmation]: SnackBar + a11y announcement.
  static void confirmation(BuildContext context, String message) {
    if (!context.mounted) {
      return;
    }
    final messenger = ScaffoldMessenger.of(context);
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(
      SnackBar(
        content: Text(message),
        duration: const Duration(seconds: 2),
        behavior: SnackBarBehavior.floating,
      ),
    );
    unawaited(whenSupported(context, message));
  }
}

/// Status chip that speaks [label] on focus and auto-announces changes only
/// while accessibility focus is on this node (dynamic liveRegion).
class A11yLiveStatusChip extends StatefulWidget {
  const A11yLiveStatusChip({
    super.key,
    required this.label,
    required this.child,
    this.alignment = Alignment.centerLeft,
  });

  final String label;
  final Widget child;
  final Alignment alignment;

  @override
  State<A11yLiveStatusChip> createState() => _A11yLiveStatusChipState();
}

class _A11yLiveStatusChipState extends State<A11yLiveStatusChip> {
  bool _a11yFocused = false;

  void _onDidGainAccessibilityFocus() {
    if (!_a11yFocused) {
      setState(() => _a11yFocused = true);
    }
  }

  void _onDidLoseAccessibilityFocus() {
    if (_a11yFocused) {
      setState(() => _a11yFocused = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Semantics(
      label: widget.label,
      liveRegion: _a11yFocused,
      excludeSemantics: true,
      onDidGainAccessibilityFocus: _onDidGainAccessibilityFocus,
      onDidLoseAccessibilityFocus: _onDidLoseAccessibilityFocus,
      child: ExcludeSemantics(
        child: Container(
          alignment: widget.alignment,
          padding: const EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: 0.1),
            borderRadius: BorderRadius.circular(4),
          ),
          child: widget.child,
        ),
      ),
    );
  }
}

/// Ensures interactive targets meet Material minimum touch size (48×48 logical px).
class MinTouchTarget extends StatelessWidget {
  const MinTouchTarget({super.key, required this.child});

  final Widget child;

  static const double minSize = 48;

  @override
  Widget build(BuildContext context) {
    return ConstrainedBox(
      constraints: const BoxConstraints(minWidth: minSize, minHeight: minSize),
      child: child,
    );
  }
}

/// Disabled control with an explicit screen reader explanation.
class UnavailableButton extends StatelessWidget {
  const UnavailableButton({
    super.key,
    required this.label,
    this.hint,
    required this.child,
  });

  final String label;
  final String? hint;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      enabled: false,
      label: label,
      hint: hint ?? A11yStrings.notAvailableYet,
      child: ExcludeSemantics(child: child),
    );
  }
}
