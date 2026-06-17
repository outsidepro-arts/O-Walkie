import 'dart:async';
import 'dart:isolate';

import 'src/session_messages.dart';
import 'src/session_worker.dart';

/// Main-isolate facade for the background session worker.
class SessionService {
  SessionService();

  Isolate? _isolate;
  SendPort? _workerPort;
  bool _disposed = false;
  Completer<void>? _disconnectInProgress;
  final StreamController<SessionWorkerMessage> _events =
      StreamController<SessionWorkerMessage>.broadcast();

  Stream<SessionWorkerMessage> get messages => _events.stream;

  bool get isRunning => _workerPort != null;

  Future<void> start() async {
    if (_disposed || _workerPort != null) {
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
    if (_disposed) return;
    final port = _workerPort;
    if (port == null) return;
    _workerPort = null;

    final completer = Completer<void>();
    late final StreamSubscription<SessionWorkerMessage> sub;
    sub = _events.stream.listen((msg) {
      if (msg is SessionShutdownCompleteMessage) {
        if (!completer.isCompleted) {
          completer.complete();
        }
        unawaited(sub.cancel());
      }
    });

    port.send(const SessionShutdownCommand());

    await completer.future.timeout(
      const Duration(seconds: 3),
      onTimeout: () {},
    );
    unawaited(sub.cancel());

    _isolate?.kill(priority: Isolate.immediate);
    _isolate = null;
  }

  void connect({
    required String host,
    required int port,
    required String channel,
    bool repeater = false,
  }) {
    if (_disposed) return;
    _workerPort?.send(SessionConnectCommand(
      host: host,
      port: port,
      channel: channel,
      repeater: repeater,
    ));
  }

  /// Switch server profile without clearing user connect intent (next/prev buttons).
  void switchServer({
    required String host,
    required int port,
    required String channel,
    bool repeater = false,
  }) {
    if (_disposed) return;
    _workerPort?.send(SessionSwitchServerCommand(
      host: host,
      port: port,
      channel: channel,
      repeater: repeater,
    ));
  }

  /// Disconnect from server and wait for worker to confirm native cleanup.
  /// Returns a Future that completes when the worker has finished disconnecting.
  /// If a disconnect is already in progress, returns the existing future.
  Future<void> disconnect() {
    if (_disposed || _workerPort == null) {
      return Future<void>.value();
    }
    if (_disconnectInProgress != null) {
      return _disconnectInProgress!.future;
    }
    final completer = Completer<void>();
    _disconnectInProgress = completer;

    late final StreamSubscription<SessionWorkerMessage> sub;
    sub = _events.stream.listen((msg) {
      if (msg is SessionDisconnectCompleteMessage) {
        if (!completer.isCompleted) {
          completer.complete();
        }
        unawaited(sub.cancel());
      }
    });

    _workerPort!.send(const SessionDisconnectCommand());

    unawaited(Future<void>.delayed(const Duration(seconds: 5)).then((_) {
      if (!completer.isCompleted) {
        completer.complete();
        unawaited(sub.cancel());
      }
    }));

    return completer.future.whenComplete(() {
      _disconnectInProgress = null;
    });
  }

  void pttDown() {
    if (_disposed) return;
    _workerPort?.send(const SessionPttDownCommand());
  }

  void pttUp({
    List<({double freqHz, int durationMs})> rogerPoints = const [],
  }) {
    if (_disposed) return;
    _workerPort?.send(SessionPttUpCommand(rogerPoints: rogerPoints));
  }

  void sendCall({
    required List<({double freqHz, int durationMs})> points,
    int repeatCount = 1,
  }) {
    if (_disposed) return;
    _workerPort?.send(SessionSendCallCommand(
      points: points,
      repeatCount: repeatCount,
    ));
  }

  void playLocalSamples(List<int> samples, {int sampleRate = 44100}) {
    if (_disposed) return;
    _workerPort?.send(SessionPlayLocalCommand(
      samples: samples,
      sampleRate: sampleRate,
    ));
  }

  void loadSoundBank({
    required List<int> pttPress,
    required List<int> pttRelease,
  }) {
    if (_disposed) return;
    _workerPort?.send(SessionSoundBankCommand(
      pttPress: pttPress,
      pttRelease: pttRelease,
    ));
  }

  void setRxVolumePercent(int percent) {
    if (_disposed) return;
    _workerPort?.send(SessionSetRxVolumeCommand(percent));
  }

  void setRepeaterMode(bool enabled) {
    if (_disposed) return;
    _workerPort?.send(SessionSetRepeaterCommand(enabled));
  }

  void punchNat() {
    if (_disposed) return;
    _workerPort?.send(const SessionPunchNatCommand());
  }

  void bindProcessNetwork(int networkHandle) {
    if (_disposed) return;
    _workerPort?.send(SessionBindProcessNetworkCommand(networkHandle));
  }

  void recoverAfterNetworkHandoff() {
    if (_disposed) return;
    _workerPort?.send(const SessionNetworkHandoffCommand());
  }

  void pauseRelay() {
    if (_disposed) return;
    _workerPort?.send(const SessionPauseRelayCommand());
  }

  void resumeRelay() {
    if (_disposed) return;
    _workerPort?.send(const SessionResumeRelayCommand());
  }

  void syncWarmCapture({
    required bool warmMicEnabled,
    required bool appInForeground,
  }) {
    if (_disposed) return;
    _workerPort?.send(SessionSyncWarmCaptureCommand(
      warmMicEnabled: warmMicEnabled,
      appInForeground: appInForeground,
    ));
  }

  void setAndroidBtVoiceRoute(bool enabled) {
    if (_disposed) return;
    _workerPort?.send(SessionSetAndroidBtVoiceRouteCommand(enabled));
  }

  void reportSignal({required int mode, required int value}) {
    if (_disposed) return;
    _workerPort?.send(SessionReportSignalCommand(mode: mode, value: value));
  }

  void clearSignal(int mode) {
    if (_disposed) return;
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
    if (_disposed || _workerPort == null) {
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
    if (_disposed) return;
    _disposed = true;
    if (_disconnectInProgress != null && !_disconnectInProgress!.isCompleted) {
      _disconnectInProgress!.complete();
    }
    unawaited(stop().then((_) => _events.close()));
  }
}
