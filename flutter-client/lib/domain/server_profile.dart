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
}
