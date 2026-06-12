import 'package:flutter/material.dart';

import '../platform/native_platform.dart';
import 'a11y_spin_box_field.dart';
import 'a11y_wheel_picker_field.dart';

/// Desktop numeric control style. Wheel is experimental (broken on Windows a11y).
enum DesktopNumericPickerStyle {
  wheel,
  spinBox,
}

/// Default picker. Wheel only on non-Windows desktop; Windows always uses spin box.
const kDesktopNumericPickerStyle = DesktopNumericPickerStyle.wheel;

/// Desktop numeric field without [Slider] (Windows AXTree-safe).
class A11yDesktopNumericField extends StatelessWidget {
  const A11yDesktopNumericField({
    super.key,
    required this.value,
    required this.min,
    required this.max,
    required this.title,
    this.suffix = '',
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

  bool get _useWheel =>
      kDesktopNumericPickerStyle == DesktopNumericPickerStyle.wheel &&
      !NativePlatform.isWindows;

  @override
  Widget build(BuildContext context) {
    if (_useWheel) {
      return A11yWheelPickerField(
        value: value,
        min: min,
        max: max,
        title: title,
        suffix: suffix,
        step: step,
        onChanged: onChanged,
        onCommit: onCommit,
      );
    }
    return A11ySpinBoxField(
      value: value,
      min: min,
      max: max,
      title: title,
      suffix: suffix,
      step: step,
      maxDigits: maxDigits,
      onChanged: onChanged,
      onCommit: onCommit,
    );
  }
}
