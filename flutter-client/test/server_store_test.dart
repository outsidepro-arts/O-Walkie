import 'package:flutter_test/flutter_test.dart';
import 'package:owalkie_app/data/server_store.dart';
import 'package:owalkie_app/domain/server_profile.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues({});
  });

  test('save and load profiles round-trip', () async {
    final store = ServerStore();
    const profiles = [
      ServerProfile(name: 'Alpha', host: '10.0.0.1', port: 5500, channel: 'a'),
      ServerProfile(name: 'Beta', host: '10.0.0.2', port: 5501, channel: 'b'),
    ];
    await store.save(profiles);
    final loaded = await store.load();
    expect(loaded.length, 2);
    expect(loaded[0].name, 'Alpha');
    expect(loaded[1].host, '10.0.0.2');
  });

  test('migrates legacy wsPort field', () async {
    SharedPreferences.setMockInitialValues({
      'items':
          '[{"name":"Legacy","host":"h","wsPort":5500,"channel":"global"}]',
    });
    final store = ServerStore();
    final loaded = await store.load();
    expect(loaded.single.port, 5500);
  });

  test('persists selected profile name', () async {
    final store = ServerStore();
    await store.setLastSelectedName('Team');
    expect(await store.getLastSelectedName(), 'Team');
  });
}
