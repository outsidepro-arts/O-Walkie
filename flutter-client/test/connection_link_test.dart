import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/domain/connection_link.dart';
import 'package:owalkie_app/domain/server_profile.dart';

void main() {
  group('connection_link', () {
    test('build and parse round-trip without name', () {
      const profile = ServerProfile(
        name: 'Team',
        host: '192.168.1.10',
        port: 5500,
        channel: 'alpha',
      );
      final link = buildConnectionDeepLink(profile, includeName: false);
      expect(link, startsWith(connectionLinkMarker));
      final parsed = parseConnectionProfileFromUri(Uri.parse(link));
      expect(parsed?.host, '192.168.1.10');
      expect(parsed?.port, 5500);
      expect(parsed?.channel, 'alpha');
      expect(parsed?.name, '192.168.1.10:5500/alpha');
    });

    test('build and parse round-trip with name', () {
      const profile = ServerProfile(
        name: 'Team Alpha',
        host: 'relay.example.com',
        port: 443,
        channel: 'ops',
      );
      final link = buildConnectionDeepLink(profile, includeName: true);
      final parsed = parseConnectionProfileFromUri(Uri.parse(link));
      expect(parsed?.name, 'Team Alpha');
    });

    test('extractConnectionLink finds embedded link', () {
      const profile = ServerProfile(
        host: '10.0.0.5',
        port: 5500,
        channel: 'global',
      );
      final link = buildConnectionDeepLink(profile, includeName: false);
      final extracted = extractConnectionLink('Join us: $link thanks');
      expect(extracted, link);
      expect(parseConnectionProfileFromLink('prefix $link'), isNotNull);
    });

    test('invalid payload returns null', () {
      expect(
        parseConnectionProfileFromUri(Uri.parse('owalkie://connect/not-base64')),
        isNull,
      );
      final badJson = base64Url.encode(utf8.encode('{"host":"","port":1}'));
      expect(
        parseConnectionProfileFromUri(
          Uri.parse('owalkie://connect/$badJson'),
        ),
        isNull,
      );
    });
  });
}
