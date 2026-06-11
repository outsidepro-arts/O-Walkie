import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../domain/signal_pattern.dart';
import 'shared_preferences_provider.dart';

final rogerPatternStoreProvider = Provider<RogerPatternStore>((ref) {
  return RogerPatternStore(ref.watch(sharedPreferencesProvider));
});

final callingPatternStoreProvider = Provider<CallingPatternStore>((ref) {
  return CallingPatternStore(ref.watch(sharedPreferencesProvider));
});

/// Roger tone patterns (Kotlin [RogerPatternStore]).
class RogerPatternStore {
  RogerPatternStore(this._prefs);

  final SharedPreferences _prefs;
  static const _customKey = 'roger_custom_items';
  static const _selectedKey = 'roger_selected_id';

  List<SignalPattern> getAllPatterns() {
    return [...BuiltInRogerPatterns.all, ..._loadCustom()];
  }

  SignalPattern getSelectedPattern() {
    final id = _prefs.getString(_selectedKey);
    return getAllPatterns().firstWhere(
      (p) => p.id == id,
      orElse: () => BuiltInRogerPatterns.none,
    );
  }

  Future<void> setSelectedPattern(String patternId) async {
    await _prefs.setString(_selectedKey, patternId);
  }

  Future<SignalPattern> saveCustomPattern(
    String name,
    List<SignalPoint> points,
  ) async {
    final cleaned = name.trim();
    final custom = _loadCustom();
    final idx = custom.indexWhere(
      (p) => p.name.toLowerCase() == cleaned.toLowerCase(),
    );
    final pattern = SignalPattern(
      id: idx >= 0
          ? custom[idx].id
          : 'custom_${DateTime.now().millisecondsSinceEpoch}',
      name: cleaned,
      points: points,
    );
    if (idx >= 0) {
      custom[idx] = pattern;
    } else {
      custom.add(pattern);
    }
    await _saveCustom(custom);
    await setSelectedPattern(pattern.id);
    return pattern;
  }

  Future<bool> updateCustomPattern(
    String patternId,
    String name,
    List<SignalPoint> points,
  ) async {
    if (name.trim().isEmpty || points.isEmpty) {
      return false;
    }
    final custom = _loadCustom();
    final idx = custom.indexWhere((p) => p.id == patternId);
    if (idx < 0) {
      return false;
    }
    custom[idx] = custom[idx].copyWith(name: name.trim(), points: points);
    await _saveCustom(custom);
    await setSelectedPattern(patternId);
    return true;
  }

  Future<bool> deleteCustomPattern(String patternId) async {
    final custom = _loadCustom();
    final before = custom.length;
    custom.removeWhere((p) => p.id == patternId);
    if (custom.length == before) {
      return false;
    }
    await _saveCustom(custom);
    if (_prefs.getString(_selectedKey) == patternId) {
      await setSelectedPattern(BuiltInRogerPatterns.none.id);
    }
    return true;
  }

  List<SignalPattern> _loadCustom() {
    final raw = _prefs.getString(_customKey);
    if (raw == null) {
      return [];
    }
    try {
      final arr = jsonDecode(raw) as List<dynamic>;
      return [
        for (final item in arr)
          if (item is Map<String, dynamic> && item['builtIn'] != true)
            SignalPattern.fromJson(item),
      ].where((p) => p.points.isNotEmpty).toList();
    } catch (_) {
      return [];
    }
  }

  Future<void> _saveCustom(List<SignalPattern> items) async {
    final encoded = jsonEncode([for (final p in items) p.toJson()]);
    await _prefs.setString(_customKey, encoded);
  }
}

/// Calling tone patterns (Kotlin [CallingPatternStore]).
class CallingPatternStore {
  CallingPatternStore(this._prefs);

  final SharedPreferences _prefs;
  static const _customKey = 'calling_custom_items';
  static const _selectedKey = 'calling_selected_id';

  List<SignalPattern> getAllPatterns() {
    return [...BuiltInCallPatterns.all, ..._loadCustom()];
  }

  SignalPattern getSelectedPattern() {
    final id = _prefs.getString(_selectedKey);
    return getAllPatterns().firstWhere(
      (p) => p.id == id,
      orElse: () => BuiltInCallPatterns.variant1,
    );
  }

  Future<void> setSelectedPattern(String patternId) async {
    await _prefs.setString(_selectedKey, patternId);
  }

  Future<SignalPattern> saveCustomPattern(
    String name,
    List<SignalPoint> points,
    int repeatCount,
  ) async {
    final cleaned = name.trim();
    final rep = repeatCount.clamp(1, 500);
    final custom = _loadCustom();
    final idx = custom.indexWhere(
      (p) => p.name.toLowerCase() == cleaned.toLowerCase(),
    );
    final pattern = SignalPattern(
      id: idx >= 0
          ? custom[idx].id
          : 'custom_${DateTime.now().millisecondsSinceEpoch}',
      name: cleaned,
      points: points,
      repeatCount: rep,
    );
    if (idx >= 0) {
      custom[idx] = pattern;
    } else {
      custom.add(pattern);
    }
    await _saveCustom(custom);
    await setSelectedPattern(pattern.id);
    return pattern;
  }

  Future<bool> updateCustomPattern(
    String patternId,
    String name,
    List<SignalPoint> points,
    int repeatCount,
  ) async {
    if (name.trim().isEmpty || points.isEmpty) {
      return false;
    }
    final custom = _loadCustom();
    final idx = custom.indexWhere((p) => p.id == patternId);
    if (idx < 0) {
      return false;
    }
    custom[idx] = custom[idx].copyWith(
      name: name.trim(),
      points: points,
      repeatCount: repeatCount.clamp(1, 500),
    );
    await _saveCustom(custom);
    await setSelectedPattern(patternId);
    return true;
  }

  Future<bool> deleteCustomPattern(String patternId) async {
    final custom = _loadCustom();
    final before = custom.length;
    custom.removeWhere((p) => p.id == patternId);
    if (custom.length == before) {
      return false;
    }
    await _saveCustom(custom);
    if (_prefs.getString(_selectedKey) == patternId) {
      await setSelectedPattern(BuiltInCallPatterns.variant1.id);
    }
    return true;
  }

  List<SignalPattern> _loadCustom() {
    final raw = _prefs.getString(_customKey);
    if (raw == null) {
      return [];
    }
    try {
      final arr = jsonDecode(raw) as List<dynamic>;
      return [
        for (final item in arr)
          if (item is Map<String, dynamic> && item['builtIn'] != true)
            SignalPattern.fromJson(item),
      ].where((p) => p.points.isNotEmpty).toList();
    } catch (_) {
      return [];
    }
  }

  Future<void> _saveCustom(List<SignalPattern> items) async {
    final encoded = jsonEncode([for (final p in items) p.toJson()]);
    await _prefs.setString(_customKey, encoded);
  }
}
