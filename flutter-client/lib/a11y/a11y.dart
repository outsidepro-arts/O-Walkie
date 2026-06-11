import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';

import '../l10n/a11y_strings.dart';

// Dynamic status updates: use Semantics(liveRegion: true, label: ...) on the
// widget whose text changes — not SemanticsService.sendAnnouncement for bulk
// updates (deprecated on Android; see flutter/flutter#165510).
//
// For Kotlin-style announceForAccessibility while a11y focus is on a control,
// use [A11yAnnounce.whenFocused].

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
