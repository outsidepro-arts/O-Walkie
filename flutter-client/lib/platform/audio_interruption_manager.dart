import 'dart:async';
import 'dart:io' show Platform;

import 'package:audio_session/audio_session.dart';

/// Listens for OS audio interruptions (phone calls, etc.) and notifies the app.
class AudioInterruptionManager {
  AudioInterruptionManager({
    required this.onInterruptBegin,
    required this.onInterruptEnd,
  });

  final void Function() onInterruptBegin;
  final void Function() onInterruptEnd;

  StreamSubscription<AudioInterruptionEvent>? _sub;
  bool _active = false;

  Future<void> start() async {
    if (_active || (!Platform.isAndroid && !Platform.isIOS)) {
      return;
    }
    final session = await AudioSession.instance;
    await session.configure(
      const AudioSessionConfiguration(
        avAudioSessionCategory: AVAudioSessionCategory.playAndRecord,
        avAudioSessionCategoryOptions: AVAudioSessionCategoryOptions.duckOthers,
        avAudioSessionMode: AVAudioSessionMode.voiceChat,
        androidAudioAttributes: AndroidAudioAttributes(
          contentType: AndroidAudioContentType.speech,
          usage: AndroidAudioUsage.voiceCommunication,
        ),
        androidAudioFocusGainType: AndroidAudioFocusGainType.gain,
      ),
    );
    _sub = session.interruptionEventStream.listen((event) {
      if (event.begin) {
        onInterruptBegin();
      } else {
        onInterruptEnd();
      }
    });
    _active = true;
  }

  Future<void> stop() async {
    await _sub?.cancel();
    _sub = null;
    _active = false;
  }
}
