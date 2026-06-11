import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

abstract final class ScreenWake {
  static Future<void> setTransmitting(bool active) async {
    if (kIsWeb || (!Platform.isAndroid && !Platform.isIOS)) {
      return;
    }
    if (active) {
      await WakelockPlus.enable();
    } else {
      await WakelockPlus.disable();
    }
  }
}
