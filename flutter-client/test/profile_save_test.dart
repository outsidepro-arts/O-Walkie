import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/domain/profile_save.dart';
import 'package:owalkie_app/domain/server_profile.dart';

void main() {
  group('applyProfileSave', () {
    const demo = ServerProfile(name: 'Demo', host: '10.0.0.1', port: 5500);
    const team = ServerProfile(name: 'Team', host: '10.0.0.2', port: 5500);

    test('new name appends profile', () {
      final result = applyProfileSave(
        profiles: const [demo],
        draft: team,
      );
      expect(result.profiles.length, 2);
      expect(result.selectedIndex, 1);
      expect(result.profiles[0].name, 'Demo');
      expect(result.profiles[1].host, '10.0.0.2');
    });

    test('same name updates existing profile', () {
      final result = applyProfileSave(
        profiles: const [demo, team],
        draft: const ServerProfile(
          name: 'Demo',
          host: '192.168.1.10',
          port: 5500,
          channel: 'ops',
        ),
      );
      expect(result.profiles.length, 2);
      expect(result.selectedIndex, 0);
      expect(result.profiles[0].host, '192.168.1.10');
      expect(result.profiles[0].channel, 'ops');
      expect(result.profiles[1].name, 'Team');
    });

    test('name match is case-insensitive', () {
      final result = applyProfileSave(
        profiles: const [demo],
        draft: const ServerProfile(name: 'demo', host: 'h', port: 5500),
      );
      expect(result.profiles.length, 1);
      expect(result.selectedIndex, 0);
      expect(result.profiles[0].host, 'h');
    });
  });
}
