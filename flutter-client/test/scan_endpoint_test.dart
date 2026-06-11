import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/domain/scan_endpoint.dart';

void main() {
  group('resolveProfileEndpoint', () {
    test('plain host uses fallback port', () {
      final endpoint = resolveProfileEndpoint('192.168.0.2', 5500);
      expect(endpoint?.host, '192.168.0.2');
      expect(endpoint?.port, 5500);
      expect(endpoint?.secure, false);
    });

    test('ws URL with explicit port', () {
      final endpoint = resolveProfileEndpoint('ws://relay.local:8443', 5500);
      expect(endpoint?.host, 'relay.local');
      expect(endpoint?.port, 8443);
      expect(endpoint?.secure, false);
    });

    test('wss URL sets secure', () {
      final endpoint = resolveProfileEndpoint('wss://relay.local:443', 5500);
      expect(endpoint?.host, 'relay.local');
      expect(endpoint?.port, 443);
      expect(endpoint?.secure, true);
    });
  });
}
