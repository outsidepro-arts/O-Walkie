import 'dart:convert';

import 'server_profile.dart';

const connectionLinkMarker = 'owalkie://connect/';

/// Build `owalkie://connect/<base64url>` (Kotlin [MainActivity.buildConnectionDeepLink]).
String buildConnectionDeepLink(
  ServerProfile profile, {
  required bool includeName,
}) {
  final payload = <String, dynamic>{
    'v': 1,
    'host': profile.host,
    'port': profile.port,
    'channel': profile.channel,
  };
  if (includeName) {
    payload['name'] = profile.name;
  }
  final encoded = base64Url
      .encode(utf8.encode(jsonEncode(payload)))
      .replaceAll('=', '');
  return '$connectionLinkMarker$encoded';
}

/// Extract a connection link embedded in arbitrary clipboard text.
String? extractConnectionLink(String text) {
  final start = text.toLowerCase().indexOf(connectionLinkMarker);
  if (start < 0) {
    return null;
  }
  var end = text.length;
  for (var i = start; i < text.length; i++) {
    if (text[i].trim().isEmpty && i > start) {
      end = i;
      break;
    }
  }
  return text.substring(start, end).trim();
}

/// Parse profile from `owalkie://connect/…` URI.
ServerProfile? parseConnectionProfileFromUri(Uri uri) {
  if (uri.scheme.toLowerCase() != 'owalkie') {
    return null;
  }
  if (uri.host.toLowerCase() != 'connect') {
    return null;
  }
  final payload = uri.pathSegments.isNotEmpty
      ? uri.pathSegments.first.trim()
      : uri.path.replaceFirst('/', '').trim();
  if (payload.isEmpty) {
    return null;
  }
  Map<String, dynamic>? json;
  try {
    final normalized = payload.padRight(
      payload.length + ((4 - payload.length % 4) % 4),
      '=',
    );
    final bytes = base64Url.decode(normalized);
    json = jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
  } catch (_) {
    return null;
  }
  final host = (json['host'] as String? ?? '').trim();
  final channel = (json['channel'] as String? ?? '').trim();
  final port = json['port'] is int ? json['port'] as int : -1;
  if (host.isEmpty || channel.isEmpty || port < 1 || port > 65535) {
    return null;
  }
  final nameRaw = (json['name'] as String? ?? '').trim();
  final name = nameRaw.isEmpty ? '$host:$port/$channel' : nameRaw;
  return ServerProfile(name: name, host: host, port: port, channel: channel);
}

ServerProfile? parseConnectionProfileFromLink(String linkText) {
  final link = extractConnectionLink(linkText) ?? linkText.trim();
  if (link.isEmpty) {
    return null;
  }
  return parseConnectionProfileFromUri(Uri.parse(link));
}
