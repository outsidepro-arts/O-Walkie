import 'package:flutter/material.dart';

import '../l10n/a11y_strings.dart';

// Dynamic status updates: use Semantics(liveRegion: true, label: ...) on the
// widget whose text changes — not SemanticsService.sendAnnouncement (deprecated
// on Android; see flutter/flutter#165510).

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
