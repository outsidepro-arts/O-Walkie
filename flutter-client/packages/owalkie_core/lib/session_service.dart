import 'dart:async';
import 'dart:isolate';

import 'src/session_messages.dart';
import 'src/session_worker.dart';

/// Main-isolate facade for the background session worker.
class SessionService {
  SessionService();

  Isolate? _isolate;
  SendPort? _workerPort;
  final StreamController<SessionWorkerMessage> _events =
      StreamController<SessionWorkerMessage>.broadcast();

  Stream<SessionWorkerMessage> get messages => _events.stream;

  bool get isRunning => _workerPort != null;

  Future<void> start() async {
    if (_workerPort != null) {
      return;
    }
    final toMain = ReceivePort();
    final workerReady = Completer<void>();
    toMain.listen((message) {
      if (message is SessionWorkerMessage) {
        _events.add(message);
        if (message is SessionLoadFailedMessage) {
          if (!workerReady.isCompleted) {
            workerReady.completeError(StateError(message.message));
          }
          return;
        }
        if (message is SessionUnsupportedMessage) {
          if (!workerReady.isCompleted) {
            workerReady.completeError(
              StateError('owalkie_core built without session transport'),
            );
          }
          return;
        }
      }
      if (message is SendPort && _workerPort == null) {
        _workerPort = message;
        if (!workerReady.isCompleted) {
          workerReady.complete();
        }
      }
    });
    _isolate = await Isolate.spawn(
      owalkieSessionWorkerEntry,
      [toMain.sendPort],
      errorsAreFatal: true,
    );
    await workerReady.future;
  }

  Future<void> stop() async {
    _workerPort?.send(const SessionShutdownCommand());
    _workerPort = null;
    _isolate?.kill(priority: Isolate.immediate);
    _isolate = null;
  }

  void connect({
    required String host,
    required int port,
    required String channel,
    bool repeater = false,
  }) {
    _workerPort?.send(SessionConnectCommand(
      host: host,
      port: port,
      channel: channel,
      repeater: repeater,
    ));
  }

  void disconnect() {
    _workerPort?.send(const SessionDisconnectCommand());
  }

  void pttDown() => _workerPort?.send(const SessionPttDownCommand());

  void pttUp({
    List<({double freqHz, int durationMs})> rogerPoints = const [],
  }) {
    _workerPort?.send(SessionPttUpCommand(rogerPoints: rogerPoints));
  }

  void sendCall({
    required List<({double freqHz, int durationMs})> points,
    int repeatCount = 1,
  }) {
    _workerPort?.send(SessionSendCallCommand(
      points: points,
      repeatCount: repeatCount,
    ));
  }

  void playLocalSamples(List<int> samples, {int sampleRate = 44100}) {
    _workerPort?.send(SessionPlayLocalCommand(
      samples: samples,
      sampleRate: sampleRate,
    ));
  }

  void loadSoundBank({
    required List<int> pttPress,
    required List<int> pttRelease,
  }) {
    _workerPort?.send(SessionSoundBankCommand(
      pttPress: pttPress,
      pttRelease: pttRelease,
    ));
  }

  void setRxVolumePercent(int percent) {
    _workerPort?.send(SessionSetRxVolumeCommand(percent));
  }

  void setRepeaterMode(bool enabled) {
    _workerPort?.send(SessionSetRepeaterCommand(enabled));
  }

  void punchNat() => _workerPort?.send(const SessionPunchNatCommand());

  void bindProcessNetwork(int networkHandle) {
    _workerPort?.send(SessionBindProcessNetworkCommand(networkHandle));
  }

  void recoverAfterNetworkHandoff() {
    _workerPort?.send(const SessionNetworkHandoffCommand());
  }

  void pauseRelay() => _workerPort?.send(const SessionPauseRelayCommand());

  void resumeRelay() => _workerPort?.send(const SessionResumeRelayCommand());

  void reportSignal({required int mode, required int value}) {
    _workerPort?.send(SessionReportSignalCommand(mode: mode, value: value));
  }

  void clearSignal(int mode) {
    _workerPort?.send(SessionClearSignalCommand(mode));
  }

  int _channelActivityRequestId = 0;

  /// One-shot channel activity probe (blocking in worker isolate).
  Future<({int resultCode, bool active})> checkChannelActivity({
    required String host,
    required int port,
    required String channel,
    int timeoutMs = 4000,
  }) async {
    if (_workerPort == null) {
      return (resultCode: -1, active: false);
    }
    final requestId = ++_channelActivityRequestId;
    final completer = Completer<({int resultCode, bool active})>();
    late final StreamSubscription<SessionWorkerMessage> sub;
    sub = messages.listen((message) {
      if (message is SessionChannelActivityResultMessage &&
          message.requestId == requestId) {
        completer.complete((resultCode: message.resultCode, active: message.active));
        unawaited(sub.cancel());
      }
    });
    _workerPort!.send(SessionCheckChannelActivityCommand(
      requestId: requestId,
      host: host,
      port: port,
      channel: channel,
      timeoutMs: timeoutMs,
    ));
    return completer.future.timeout(
      Duration(milliseconds: timeoutMs + 2000),
      onTimeout: () {
        unawaited(sub.cancel());
        return (resultCode: -1, active: false);
      },
    );
  }

  void dispose() {
    unawaited(stop());
    _events.close();
  }
}
