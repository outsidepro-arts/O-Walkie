import 'dart:typed_data';

import 'session_relay_bindings.dart';

/// Local speaker playback via native miniaudio.
abstract final class LocalPcmPlayer {
  static void playBlocking(Int16List pcm, {required int sampleRate}) {
    if (pcm.isEmpty) {
      return;
    }
    SessionRelayBindings.open().playLocalPcm(pcm, sampleRate: sampleRate);
  }

  static void startLoop(Int16List pcm, {required int sampleRate}) {
    if (pcm.isEmpty) {
      return;
    }
    SessionRelayBindings.open().startLocalPcmLoop(pcm, sampleRate: sampleRate);
  }

  static void stopLoop() {
    SessionRelayBindings.open().stopLocalPcmLoop();
  }
}
