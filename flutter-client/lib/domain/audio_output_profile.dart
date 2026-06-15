import '../l10n/app_strings.dart';

class AudioOutputProfile {
  const AudioOutputProfile({
    required this.id,
    required this.title,
    required this.aaudioUsage,
    required this.aaudioContentType,
    required this.audioManagerMode,
    required this.enableBtSco,
    required this.iosMode,
    required this.iosDefaultToSpeaker,
    required this.iosAllowBluetoothA2dp,
    required this.iosDuckOthers,
  });

  final String id;

  final String title;

  final int aaudioUsage;

  final int aaudioContentType;

  final int audioManagerMode;

  final bool enableBtSco;

  final String iosMode;

  final bool iosDefaultToSpeaker;

  final bool iosAllowBluetoothA2dp;

  final bool iosDuckOthers;

  String get displayTitle => AppStrings.audioOutputProfileTitle(id, fallback: title);

  static const media = AudioOutputProfile(
    id: 'media',
    title: 'Media',
    aaudioUsage: 1,
    aaudioContentType: 2,
    audioManagerMode: 0,
    enableBtSco: false,
    iosMode: 'default',
    iosDefaultToSpeaker: true,
    iosAllowBluetoothA2dp: false,
    iosDuckOthers: false,
  );

  static const voiceCall = AudioOutputProfile(
    id: 'voice_call',
    title: 'Voice call',
    aaudioUsage: 2,
    aaudioContentType: 1,
    audioManagerMode: 3,
    enableBtSco: false,
    iosMode: 'voiceChat',
    iosDefaultToSpeaker: true,
    iosAllowBluetoothA2dp: false,
    iosDuckOthers: false,
  );

  static const voiceCallBt = AudioOutputProfile(
    id: 'voice_call_bt',
    title: 'Voice call + BT',
    aaudioUsage: 2,
    aaudioContentType: 1,
    audioManagerMode: 3,
    enableBtSco: true,
    iosMode: 'voiceChat',
    iosDefaultToSpeaker: false,
    iosAllowBluetoothA2dp: true,
    iosDuckOthers: false,
  );

  static const game = AudioOutputProfile(
    id: 'game',
    title: 'Game',
    aaudioUsage: 14,
    aaudioContentType: 1,
    audioManagerMode: 0,
    enableBtSco: false,
    iosMode: 'gameChat',
    iosDefaultToSpeaker: true,
    iosAllowBluetoothA2dp: false,
    iosDuckOthers: false,
  );

  static const raw = AudioOutputProfile(
    id: 'raw',
    title: 'Unprocessed',
    aaudioUsage: 1,
    aaudioContentType: 0,
    audioManagerMode: 0,
    enableBtSco: false,
    iosMode: 'measurement',
    iosDefaultToSpeaker: true,
    iosAllowBluetoothA2dp: false,
    iosDuckOthers: false,
  );

  static const notification = AudioOutputProfile(
    id: 'notification',
    title: 'Notification',
    aaudioUsage: 5,
    aaudioContentType: 4,
    audioManagerMode: 0,
    enableBtSco: false,
    iosMode: 'voiceChat',
    iosDefaultToSpeaker: true,
    iosAllowBluetoothA2dp: false,
    iosDuckOthers: true,
  );

  static const all = [media, voiceCall, voiceCallBt, game, raw, notification];

  static AudioOutputProfile byId(String id) =>
      all.firstWhere((p) => p.id == id, orElse: () => media);
}