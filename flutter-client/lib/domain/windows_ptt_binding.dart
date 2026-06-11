/// Windows global PTT key binding (Win32 VK + modifier bitmask).
class WindowsPttBinding {
  const WindowsPttBinding({
    required this.vkey,
    required this.mods,
    required this.displayName,
  });

  final int vkey;
  final int mods;
  final String displayName;

  bool get assigned => vkey > 0;

  @override
  bool operator ==(Object other) {
    return other is WindowsPttBinding &&
        other.vkey == vkey &&
        other.mods == mods &&
        other.displayName == displayName;
  }

  @override
  int get hashCode => Object.hash(vkey, mods, displayName);

  Map<String, dynamic> toJson() => {
        'vkey': vkey,
        'mods': mods,
        'displayName': displayName,
      };

  factory WindowsPttBinding.fromJson(Map<String, dynamic> json) {
    return WindowsPttBinding(
      vkey: json['vkey'] as int? ?? 0,
      mods: json['mods'] as int? ?? 0,
      displayName: json['displayName'] as String? ?? '',
    );
  }

  factory WindowsPttBinding.fromPlatformMap(Map<dynamic, dynamic> map) {
    return WindowsPttBinding(
      vkey: map['vkey'] as int? ?? 0,
      mods: map['mods'] as int? ?? 0,
      displayName: map['displayName'] as String? ?? '',
    );
  }
}
