import '../l10n/app_strings.dart';

class MicrophoneSourceOption {
  const MicrophoneSourceOption({
    required this.id,
    required this.title,
    required this.inputPreset,
  });

  final String id;

  /// Native fallback title (Android Kotlin strings). UI uses [displayTitle].
  final String title;

  /// Platform-specific native profile index (Android: miniaudio AAudio preset).
  final int inputPreset;

  String get displayTitle => AppStrings.microphoneSourceTitle(id, fallback: title);
}
