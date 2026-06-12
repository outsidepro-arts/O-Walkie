import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Scroll-wheel numeric picker (no [Slider] — Windows AXTree-safe).
class A11yWheelPickerField extends StatefulWidget {
  const A11yWheelPickerField({
    super.key,
    required this.value,
    required this.min,
    required this.max,
    required this.title,
    this.suffix = '',
    this.step = 1,
    required this.onChanged,
    required this.onCommit,
  });

  final int value;
  final int min;
  final int max;
  final String title;
  final String suffix;
  final int step;
  final ValueChanged<int> onChanged;
  final ValueChanged<int> onCommit;

  @override
  State<A11yWheelPickerField> createState() => _A11yWheelPickerFieldState();
}

class _A11yWheelPickerFieldState extends State<A11yWheelPickerField> {
  static const _itemExtent = 40.0;
  static const _wheelHeight = 160.0;

  late FixedExtentScrollController _scrollController;
  late int _itemCount;

  @override
  void initState() {
    super.initState();
    _itemCount = _computeItemCount();
    _scrollController = FixedExtentScrollController(
      initialItem: _indexForValue(widget.value),
    );
  }

  @override
  void didUpdateWidget(covariant A11yWheelPickerField oldWidget) {
    super.didUpdateWidget(oldWidget);
    final newCount = _computeItemCount();
    if (newCount != _itemCount) {
      _itemCount = newCount;
    }
    if (oldWidget.value != widget.value) {
      final target = _indexForValue(widget.value);
      if (_scrollController.hasClients &&
          _scrollController.selectedItem != target) {
        _scrollController.jumpToItem(target);
      }
    }
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  int _computeItemCount() {
    final step = widget.step.clamp(1, widget.max - widget.min);
    return ((widget.max - widget.min) ~/ step) + 1;
  }

  int _indexForValue(int raw) {
    final step = widget.step.clamp(1, widget.max - widget.min);
    final clamped = raw.clamp(widget.min, widget.max);
    return ((clamped - widget.min) ~/ step).clamp(0, _itemCount - 1);
  }

  int _valueForIndex(int index) {
    final step = widget.step.clamp(1, widget.max - widget.min);
    return (widget.min + index * step).clamp(widget.min, widget.max);
  }

  void _selectIndex(int index) {
    final value = _valueForIndex(index);
    if (value == widget.value) {
      return;
    }
    widget.onChanged(value);
    widget.onCommit(value);
  }

  void _stepBy(int deltaSteps) {
    final nextIndex =
        (_indexForValue(widget.value) + deltaSteps).clamp(0, _itemCount - 1);
    if (_scrollController.hasClients) {
      _scrollController.animateToItem(
        nextIndex,
        duration: const Duration(milliseconds: 200),
        curve: Curves.easeOut,
      );
    }
    _selectIndex(nextIndex);
  }

  KeyEventResult _onKeyEvent(FocusNode node, KeyEvent event) {
    if (event is! KeyDownEvent) {
      return KeyEventResult.ignored;
    }
    switch (event.logicalKey) {
      case LogicalKeyboardKey.arrowDown:
      case LogicalKeyboardKey.arrowRight:
        _stepBy(1);
        return KeyEventResult.handled;
      case LogicalKeyboardKey.arrowUp:
      case LogicalKeyboardKey.arrowLeft:
        _stepBy(-1);
        return KeyEventResult.handled;
      default:
        return KeyEventResult.ignored;
    }
  }

  String _formatValue(int value) {
    final suffix = widget.suffix;
    return suffix.isEmpty ? '$value' : '$value$suffix';
  }

  @override
  Widget build(BuildContext context) {
    final value = widget.value.clamp(widget.min, widget.max);
    final canIncrease = value < widget.max;
    final canDecrease = value > widget.min;
    final nextValue = canIncrease ? _valueForIndex(_indexForValue(value) + 1) : null;
    final prevValue = canDecrease ? _valueForIndex(_indexForValue(value) - 1) : null;
    final label = '${widget.title} ${_formatValue(value)}';

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          widget.title,
          style: Theme.of(context).textTheme.bodyMedium,
        ),
        const SizedBox(height: 4),
        Semantics(
          label: label,
          increasedValue: nextValue != null ? _formatValue(nextValue) : null,
          decreasedValue: prevValue != null ? _formatValue(prevValue) : null,
          onIncrease: canIncrease ? () => _stepBy(1) : null,
          onDecrease: canDecrease ? () => _stepBy(-1) : null,
          child: Focus(
            onKeyEvent: _onKeyEvent,
            child: ExcludeSemantics(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  border: Border.all(
                    color: Theme.of(context).colorScheme.outlineVariant,
                  ),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: SizedBox(
                  height: _wheelHeight,
                  child: Stack(
                    children: [
                      ListWheelScrollView.useDelegate(
                        controller: _scrollController,
                        itemExtent: _itemExtent,
                        diameterRatio: 1.6,
                        perspective: 0.003,
                        physics: const FixedExtentScrollPhysics(),
                        onSelectedItemChanged: _selectIndex,
                        childDelegate: ListWheelChildBuilderDelegate(
                          childCount: _itemCount,
                          builder: (context, index) {
                            final itemValue = _valueForIndex(index);
                            final selected = itemValue == value;
                            return Center(
                              child: Text(
                                _formatValue(itemValue),
                                style: Theme.of(context)
                                    .textTheme
                                    .titleMedium
                                    ?.copyWith(
                                      fontWeight: selected
                                          ? FontWeight.bold
                                          : FontWeight.normal,
                                      color: selected
                                          ? Theme.of(context)
                                              .colorScheme
                                              .primary
                                          : Theme.of(context)
                                              .colorScheme
                                              .onSurface
                                              .withValues(alpha: 0.55),
                                    ),
                              ),
                            );
                          },
                        ),
                      ),
                      IgnorePointer(
                        child: Center(
                          child: Container(
                            height: _itemExtent,
                            margin: const EdgeInsets.symmetric(horizontal: 8),
                            decoration: BoxDecoration(
                              border: Border.symmetric(
                                horizontal: BorderSide(
                                  color: Theme.of(context)
                                      .colorScheme
                                      .primary
                                      .withValues(alpha: 0.35),
                                ),
                              ),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}
