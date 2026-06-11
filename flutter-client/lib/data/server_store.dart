import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../domain/server_profile.dart';

const _itemsKey = 'items';
const _selectedNameKey = 'selected_name';

final serverStoreProvider = Provider<ServerStore>((ref) => ServerStore());

/// Persists connection profiles (mirrors Kotlin `ServerStore`).
class ServerStore {
  Future<List<ServerProfile>> load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_itemsKey);
    if (raw == null || raw.isEmpty) {
      return [];
    }
    try {
      final decoded = jsonDecode(raw);
      if (decoded is! List) {
        return [];
      }
      return decoded
          .whereType<Map>()
          .map((e) => ServerProfile.fromJson(Map<String, dynamic>.from(e)))
          .toList();
    } catch (_) {
      return [];
    }
  }

  Future<void> save(List<ServerProfile> items) async {
    final prefs = await SharedPreferences.getInstance();
    final encoded = jsonEncode(items.map((e) => e.toJson()).toList());
    await prefs.setString(_itemsKey, encoded);
  }

  Future<String?> getLastSelectedName() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_selectedNameKey);
  }

  Future<void> setLastSelectedName(String name) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_selectedNameKey, name);
  }
}
