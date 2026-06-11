import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum ScreenOrientationMode {
  followSystem,
  portrait,
  landscape,
}

final orientationStoreProvider = Provider<OrientationStore>((ref) => OrientationStore());

class OrientationStore {
  static const _key = 'screen_orientation';

  Future<ScreenOrientationMode> load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key);
    return ScreenOrientationMode.values.firstWhere(
      (m) => m.name == raw,
      orElse: () => ScreenOrientationMode.followSystem,
    );
  }

  Future<void> save(ScreenOrientationMode mode) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_key, mode.name);
    await apply(mode);
  }

  Future<void> apply(ScreenOrientationMode mode) async {
    switch (mode) {
      case ScreenOrientationMode.followSystem:
        await SystemChrome.setPreferredOrientations(DeviceOrientation.values);
      case ScreenOrientationMode.portrait:
        await SystemChrome.setPreferredOrientations([
          DeviceOrientation.portraitUp,
          DeviceOrientation.portraitDown,
        ]);
      case ScreenOrientationMode.landscape:
        await SystemChrome.setPreferredOrientations([
          DeviceOrientation.landscapeLeft,
          DeviceOrientation.landscapeRight,
        ]);
    }
  }
}
