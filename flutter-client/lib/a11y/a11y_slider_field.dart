import 'package:flutter/material.dart';

import '../platform/native_platform.dart';
import 'a11y.dart';

/// Slider row matching home RX volume semantics and tab order.
///
/// On desktop, avoids [ListView] (use [SingleChildScrollView] in parent) and
/// disables the slider value overlay — both trigger Windows AXTree bugs
/// (flutter/flutter#182444, flutter/flutter#98099).
class A11ySliderField extends StatefulWidget {
  const A11ySliderField({
    super.key,
    required this.value,
    required this.min,
    required this.max,
    required this.divisions,
    required this.title,
    required this.displayValue,
    required this.semanticsLabel,
    required this.semanticsValue,
    this.increasedValue,
    this.decreasedValue,
    required this.onChanged,
    required this.onChangeEnd,
    this.announceOnChangeEnd,
  });

  final double value;
  final double min;
  final double max;
  final int divisions;
  final String title;
  final String displayValue;
  final String semanticsLabel;
  final String semanticsValue;
  final String? increasedValue;
  final String? decreasedValue;
  final ValueChanged<double> onChanged;
  final ValueChanged<double> onChangeEnd;
  final String Function(double value)? announceOnChangeEnd;

  @override
  State<A11ySliderField> createState() => _A11ySliderFieldState();
}

class _A11ySliderFieldState extends State<A11ySliderField> {
  bool _a11yFocused = false;

  void _onDidGainAccessibilityFocus() {
    _a11yFocused = true;
  }

  void _onDidLoseAccessibilityFocus() {
    _a11yFocused = false;
  }

  void _onChangeEnd(double value) {
    widget.onChangeEnd(value);
    final message = widget.announceOnChangeEnd?.call(value);
    if (message != null) {
      A11yAnnounce.whenFocused(
        context,
        focused: _a11yFocused,
        message: message,
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final value = widget.value.clamp(widget.min, widget.max);
    Widget slider = Slider(
      value: value,
      min: widget.min,
      max: widget.max,
      divisions: widget.divisions,
      onChanged: widget.onChanged,
      onChangeEnd: _onChangeEnd,
    );

    if (NativePlatform.isDesktop) {
      slider = SliderTheme(
        data: SliderTheme.of(context).copyWith(
          showValueIndicator: ShowValueIndicator.never,
        ),
        child: slider,
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        ExcludeSemantics(
          child: Row(
            children: [
              Expanded(
                child: Text(
                  widget.title,
                  style: Theme.of(context).textTheme.bodyMedium,
                ),
              ),
              Text(
                widget.displayValue,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      fontWeight: FontWeight.bold,
                    ),
              ),
            ],
          ),
        ),
        Semantics(
          slider: true,
          label: widget.semanticsLabel,
          value: widget.semanticsValue,
          increasedValue: widget.increasedValue,
          decreasedValue: widget.decreasedValue,
          onDidGainAccessibilityFocus: _onDidGainAccessibilityFocus,
          onDidLoseAccessibilityFocus: _onDidLoseAccessibilityFocus,
          child: slider,
        ),
      ],
    );
  }
}
