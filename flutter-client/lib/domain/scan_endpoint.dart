/// Resolved WebSocket endpoint for channel activity probes (Kotlin [ParsedScanEndpoint]).
class ScanEndpoint {
  const ScanEndpoint({
    required this.host,
    required this.port,
    required this.secure,
  });

  final String host;
  final int port;
  final bool secure;
}

ScanEndpoint? resolveProfileEndpoint(String rawHost, int fallbackWsPort) {
  var value = rawHost.trim();
  if (value.isEmpty) {
    return null;
  }
  var secure = false;
  final lowered = value.toLowerCase();
  if (lowered.startsWith('wss://')) {
    secure = true;
    value = value.substring(6);
  } else if (lowered.startsWith('https://')) {
    secure = true;
    value = value.substring(8);
  } else if (lowered.startsWith('ws://')) {
    value = value.substring(5);
  } else if (lowered.startsWith('http://')) {
    value = value.substring(7);
  }
  value = value.split('/').first.split('?').first.split('#').first.trim();
  value = value.contains('@') ? value.substring(value.lastIndexOf('@') + 1) : value;
  if (value.isEmpty) {
    return null;
  }
  var port = fallbackWsPort;
  var host = value;
  if (host.startsWith('[')) {
    final closing = host.indexOf(']');
    if (closing <= 0) {
      return null;
    }
    final ipv6 = host.substring(1, closing).trim();
    final rest = host.substring(closing + 1);
    if (rest.startsWith(':')) {
      final parsedPort = int.tryParse(rest.substring(1));
      if (parsedPort == null) {
        return null;
      }
      port = parsedPort;
    }
    host = ipv6;
  } else {
    final colonCount = ':'.allMatches(host).length;
    if (colonCount == 1) {
      final idx = host.lastIndexOf(':');
      final maybePort = int.tryParse(host.substring(idx + 1));
      if (maybePort != null) {
        port = maybePort;
        host = host.substring(0, idx);
      }
    }
  }
  if (host.isEmpty || port < 1 || port > 65535) {
    return null;
  }
  return ScanEndpoint(host: host, port: port, secure: secure);
}
