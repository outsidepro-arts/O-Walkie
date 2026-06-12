import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../platform/native_platform.dart';
import 'a11y.dart';

/// Slider row matching home RX volume semantics and tab order.
///
/// [semanticStep] applies to screen-reader increase/decrease and arrow keys.
/// Mouse/drag still uses one [divisions] step per pixel snap.
///
/// One semantics node + one focus node: visual [Slider] is excluded from both
/// trees so TalkBack/Narrator do not trap on a duplicate control.
///
/// On desktop, disables the slider value overlay (Windows AXTree bugs).
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
    this.semanticStep = 5,
    this.formatStepValue,
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
  final double semanticStep;
  final String Function(double value)? formatStepValue;
  final ValueChanged<double> onChanged;
  final ValueChanged<double> onChangeEnd;
  final String Function(double value)? announceOnChangeEnd;

  @override
  State<A11ySliderField> createState() => _A11ySliderFieldState();
}

class _A11ySliderFieldState extends State<A11ySliderField> {
  bool _a11yFocused = false;
  late final FocusNode _focusNode;

  @override
  void initState() {
    super.initState();
    _focusNode = FocusNode();
  }

  @override
  void dispose() {
    _focusNode.dispose();
    super.dispose();
  }

  void _onDidGainAccessibilityFocus() {
    _a11yFocused = true;
  }

  void _onDidLoseAccessibilityFocus() {
    _a11yFocused = false;
  }

  String _formatStepValue(double value) {
    return widget.formatStepValue?.call(value) ?? '${value.round()}';
  }

  double _quantizeSemantic(double raw) {
    final step = widget.semanticStep;
    if (step <= 0) {
      return raw.clamp(widget.min, widget.max);
    }
    final steps = ((raw - widget.min) / step).round();
    return (widget.min + steps * step).clamp(widget.min, widget.max);
  }

  void _applySemanticDelta(double delta) {
    final next = _quantizeSemantic(widget.value + delta);
    if (next == widget.value) {
      return;
    }
    widget.onChanged(next);
    widget.onChangeEnd(next);
    final message = widget.announceOnChangeEnd?.call(next);
    if (message != null) {
      A11yAnnounce.whenFocused(
        context,
        focused: _a11yFocused,
        message: message,
      );
    }
  }

  void _increase() {
    if (widget.value >= widget.max) {
      return;
    }
    _applySemanticDelta(widget.semanticStep);
  }

  void _decrease() {
    if (widget.value <= widget.min) {
      return;
    }
    _applySemanticDelta(-widget.semanticStep);
  }

  KeyEventResult _onKeyEvent(FocusNode node, KeyEvent event) {
    if (event is! KeyDownEvent) {
      return KeyEventResult.ignored;
    }
    switch (event.logicalKey) {
      case LogicalKeyboardKey.arrowRight:
      case LogicalKeyboardKey.arrowUp:
        _increase();
        return KeyEventResult.handled;
      case LogicalKeyboardKey.arrowLeft:
      case LogicalKeyboardKey.arrowDown:
        _decrease();
        return KeyEventResult.handled;
      default:
        return KeyEventResult.ignored;
    }
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
    final canIncrease = value < widget.max;
    final canDecrease = value > widget.min;
    final increased = canIncrease
        ? _formatStepValue(_quantizeSemantic(value + widget.semanticStep))
        : null;
    final decreased = canDecrease
        ? _formatStepValue(_quantizeSemantic(value - widget.semanticStep))
        : null;

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
          increasedValue: increased,
          decreasedValue: decreased,
          onIncrease: canIncrease ? _increase : null,
          onDecrease: canDecrease ? _decrease : null,
          onDidGainAccessibilityFocus: _onDidGainAccessibilityFocus,
          onDidLoseAccessibilityFocus: _onDidLoseAccessibilityFocus,
          child: Focus(
            focusNode: _focusNode,
            onKeyEvent: _onKeyEvent,
            child: ExcludeSemantics(
              child: Focus(
                canRequestFocus: false,
                skipTraversal: true,
                descendantsAreFocusable: false,
                child: slider,
              ),
            ),
          ),
        ),
      ],
    );
  }
}
