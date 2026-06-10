import 'package:flutter/material.dart';
import 'package:flutter/semantics.dart';

import '../l10n/a11y_strings.dart';

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

/// Announces [message] to screen readers when UI [liveRegion] is insufficient.
void announceToScreenReader(BuildContext context, String message) {
  final view = View.maybeOf(context);
  if (view == null) {
    return;
  }
  SemanticsService.sendAnnouncement(view, message, TextDirection.ltr);
}

/// Disabled control with an explicit screen reader explanation.
class UnavailableButton extends StatelessWidget {
  const UnavailableButton({
    super.key,
    required this.label,
    this.hint = A11yStrings.notAvailableYet,
    required this.child,
  });

  final String label;
  final String hint;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      enabled: false,
      label: label,
      hint: hint,
      child: ExcludeSemantics(child: child),
    );
  }
}
