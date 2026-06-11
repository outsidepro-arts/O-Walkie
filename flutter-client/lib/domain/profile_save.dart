import 'server_profile.dart';

class ProfileSaveResult {
  const ProfileSaveResult({
    required this.profiles,
    required this.selectedIndex,
  });

  final List<ServerProfile> profiles;
  final int selectedIndex;
}

/// Mirrors Kotlin `saveServerButton`: upsert by profile name (case-insensitive).
ProfileSaveResult applyProfileSave({
  required List<ServerProfile> profiles,
  required ServerProfile draft,
}) {
  final list = [...profiles];
  final normalized = ServerProfile(
    name: draft.name.trim(),
    host: draft.host.trim(),
    port: draft.port,
    channel: draft.channel.trim(),
    repeater: draft.repeater,
  );
  final existing = list.indexWhere(
    (p) => p.name.toLowerCase() == normalized.name.toLowerCase(),
  );
  if (existing >= 0) {
    list[existing] = normalized;
    return ProfileSaveResult(profiles: list, selectedIndex: existing);
  }
  list.add(normalized);
  return ProfileSaveResult(profiles: list, selectedIndex: list.length - 1);
}

String? validateProfileDraft(ServerProfile draft) {
  if (draft.name.trim().isEmpty) {
    return 'Enter a server name.';
  }
  if (draft.host.trim().isEmpty) {
    return 'Enter server host.';
  }
  if (draft.channel.trim().isEmpty) {
    return 'Enter channel name.';
  }
  if (draft.port < 1 || draft.port > 65535) {
    return 'Port must be between 1 and 65535.';
  }
  return null;
}
