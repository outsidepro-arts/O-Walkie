import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Numeric stepper with [TextField] and +/- buttons (Windows AXTree-safe).
class A11ySpinBoxField extends StatefulWidget {
  const A11ySpinBoxField({
    super.key,
    required this.value,
    required this.min,
    required this.max,
    required this.title,
    required this.suffix,
    this.step = 1,
    this.maxDigits = 3,
    required this.onChanged,
    required this.onCommit,
  });

  final int value;
  final int min;
  final int max;
  final String title;
  final String suffix;
  final int step;
  final int maxDigits;
  final ValueChanged<int> onChanged;
  final ValueChanged<int> onCommit;

  @override
  State<A11ySpinBoxField> createState() => _A11ySpinBoxFieldState();
}

class _A11ySpinBoxFieldState extends State<A11ySpinBoxField> {
  late final TextEditingController _controller;
  late final FocusNode _focusNode;

  @override
  void initState() {
    super.initState();
    _controller = TextEditingController(text: '${widget.value}');
    _focusNode = FocusNode();
    _focusNode.addListener(_onFocusChange);
  }

  @override
  void didUpdateWidget(covariant A11ySpinBoxField oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.value != widget.value && !_focusNode.hasFocus) {
      _controller.text = '${widget.value}';
    }
  }

  @override
  void dispose() {
    _focusNode
      ..removeListener(_onFocusChange)
      ..dispose();
    _controller.dispose();
    super.dispose();
  }

  void _onFocusChange() {
    if (!_focusNode.hasFocus) {
      _commitFromField(_controller.text);
    }
  }

  int _clamp(int raw) => raw.clamp(widget.min, widget.max);

  void _apply(int next) {
    final clamped = _clamp(next);
    if (clamped == widget.value) {
      if (!_focusNode.hasFocus) {
        _controller.text = '$clamped';
      }
      return;
    }
    widget.onChanged(clamped);
    widget.onCommit(clamped);
    if (!_focusNode.hasFocus) {
      _controller.text = '$clamped';
    }
  }

  void _decrease() {
    _apply(widget.value - widget.step);
  }

  void _increase() {
    _apply(widget.value + widget.step);
  }

  void _commitFromField(String text) {
    final parsed = int.tryParse(text.trim());
    if (parsed == null) {
      _controller.text = '${widget.value}';
      return;
    }
    _apply(parsed);
  }

  @override
  Widget build(BuildContext context) {
    final canDecrease = widget.value > widget.min;
    final canIncrease = widget.value < widget.max;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            IconButton(
              tooltip: '−${widget.step}',
              onPressed: canDecrease ? _decrease : null,
              icon: const Icon(Icons.remove),
            ),
            Expanded(
              child: TextField(
                controller: _controller,
                focusNode: _focusNode,
                keyboardType: TextInputType.number,
                textInputAction: TextInputAction.done,
                textAlign: TextAlign.center,
                inputFormatters: [
                  FilteringTextInputFormatter.digitsOnly,
                  LengthLimitingTextInputFormatter(widget.maxDigits),
                ],
                decoration: InputDecoration(
                  labelText: widget.title,
                  suffixText: widget.suffix,
                ),
                onSubmitted: _commitFromField,
              ),
            ),
            IconButton(
              tooltip: '+${widget.step}',
              onPressed: canIncrease ? _increase : null,
              icon: const Icon(Icons.add),
            ),
          ],
        ),
      ],
    );
  }
}
