import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/server_store.dart';
import 'package:owalkie_app/data/shared_preferences_provider.dart';
import 'package:owalkie_app/domain/server_profile.dart';
import 'package:owalkie_app/features/home/home_screen_controller.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('HomeScreenController profile selection', () {
    late ProviderContainer container;

    setUp(() async {
      SharedPreferences.setMockInitialValues({});
      final prefs = await SharedPreferences.getInstance();
      container = ProviderContainer(
        overrides: [
          sharedPreferencesProvider.overrideWithValue(prefs),
        ],
      );
    });

    tearDown(() {
      container.dispose();
    });

    test('connectToSelectedProfile uses saved profile not stale draft', () async {
      final store = container.read(serverStoreProvider);
      const team = ServerProfile(
        name: 'Team',
        host: '192.168.1.10',
        port: 5501,
        channel: 'alpha',
      );
      const demo = ServerProfile(name: 'Demo', host: '127.0.0.1', port: 5500);
      await store.save([team, demo]);
      await store.setLastSelectedName('Team');

      final controller = container.read(homeScreenControllerProvider.notifier);

      // Simulate stale UI draft before async profile load.
      controller.state = controller.state.copyWith(
        profiles: [team, demo],
        selectedServerIndex: 0,
        draftProfile: demo,
      );

      controller.syncDraftFromSelectedProfile();

      expect(controller.state.draftProfile, team);
      expect(controller.state.profile.host, '192.168.1.10');
      expect(controller.state.profile.port, 5501);
    });

    test('selectProfile switches saved profile while connected', () {
      const team = ServerProfile(
        name: 'Team',
        host: '192.168.1.10',
        port: 5501,
        channel: 'alpha',
      );
      const demo = ServerProfile(
        name: 'Demo',
        host: '127.0.0.1',
        port: 5500,
        channel: 'beta',
      );
      final controller = container.read(homeScreenControllerProvider.notifier);
      controller.state = controller.state.copyWith(
        profiles: [team, demo],
        selectedServerIndex: 0,
        draftProfile: team,
        isConnected: true,
      );

      controller.selectProfile(1);

      expect(controller.state.selectedServerIndex, 1);
      expect(controller.state.draftProfile, demo);
      expect(controller.state.lastError, isNull);
    });

    test('selectProfile ignores same index', () {
      const team = ServerProfile(
        name: 'Team',
        host: '192.168.1.10',
        port: 5501,
        channel: 'alpha',
      );
      final controller = container.read(homeScreenControllerProvider.notifier);
      controller.state = controller.state.copyWith(
        profiles: [team],
        selectedServerIndex: 0,
        draftProfile: team,
        isConnected: true,
      );

      controller.selectProfile(0);

      expect(controller.state.selectedServerIndex, 0);
      expect(controller.state.draftProfile, team);
    });
  });
}
