class ServerProfile {
  const ServerProfile({
    this.name = 'Demo',
    this.host = '',
    this.port = 5500,
    this.channel = 'global',
    this.repeater = false,
  });

  final String name;
  final String host;
  final int port;
  final String channel;
  final bool repeater;

  ServerProfile copyWith({
    String? name,
    String? host,
    int? port,
    String? channel,
    bool? repeater,
  }) {
    return ServerProfile(
      name: name ?? this.name,
      host: host ?? this.host,
      port: port ?? this.port,
      channel: channel ?? this.channel,
      repeater: repeater ?? this.repeater,
    );
  }

  Map<String, dynamic> toJson() => {
        'name': name,
        'host': host,
        'port': port,
        'channel': channel,
        if (repeater) 'repeater': true,
      };

  factory ServerProfile.fromJson(Map<String, dynamic> json) {
    final port = _readPort(json);
    return ServerProfile(
      name: json['name'] as String? ?? '',
      host: json['host'] as String? ?? '',
      port: port,
      channel: json['channel'] as String? ?? 'global',
      repeater: json['repeater'] as bool? ?? false,
    );
  }

  static int _readPort(Map<String, dynamic> json) {
    if (json['port'] is int) {
      return json['port'] as int;
    }
    if (json['wsPort'] is int) {
      return json['wsPort'] as int;
    }
    if (json['udpPort'] is int) {
      return json['udpPort'] as int;
    }
    return 5500;
  }
}
