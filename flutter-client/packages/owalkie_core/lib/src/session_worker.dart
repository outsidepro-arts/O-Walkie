import 'dart:async';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

import '../owalkie_core_bindings_generated.dart';
import 'native_library.dart';
import 'session_event_type.dart';
import 'session_messages.dart';
import 'session_relay_bindings.dart';

/// Background isolate: owns all session + audio FFI calls.
@pragma('vm:entry-point')
void owalkieSessionWorkerEntry(List<dynamic> args) {
  final mainSendPort = args[0] as SendPort;
  final worker = _SessionWorker(mainSendPort);
  worker.run();
}

class _SessionWorker {
  _SessionWorker(this._mainPort);

  final SendPort _mainPort;
  late final SessionRelayBindings _relay;
  final ReceivePort _commands = ReceivePort();

  int _sessionId = 0;
  bool _desiredConnected = false;
  bool _connectLoopRunning = false;
  Timer? _pollTimer;

  void run() {
    try {
      final lib = openOwalkieCoreLibrary();
      _relay = SessionRelayBindings(lib);
      if (!_relay.hasSession) {
        _mainPort.send(const SessionWorkerMessage.sessionUnsupported());
        return;
      }
      final meta = OwalkieCoreBindings(lib);
      _mainPort.send(SessionWorkerMessage.coreInfo(
        version: meta.owalkie_flutter_core_version().cast<Utf8>().toDartString(),
        protocolVersion: meta.owalkie_flutter_protocol_version(),
      ));
    } catch (e) {
      _mainPort.send(SessionWorkerMessage.loadFailed('$e'));
      return;
    }
    _mainPort.send(_commands.sendPort);
    _pollTimer = Timer.periodic(const Duration(milliseconds: 30), (_) => _drainNativeEvents());
    _commands.listen(_onCommand);
  }

  void _onCommand(dynamic message) {
    if (message is! SessionCommand) {
      return;
    }
    switch (message) {
      case SessionConnectCommand():
        _startConnect(message);
      case SessionDisconnectCommand():
        _disconnect();
      case SessionPttDownCommand():
        _pttDown();
      case SessionPttUpCommand():
        _pttUp();
      case SessionSetRxVolumeCommand(:final percent):
        _relay.setRxVolumePercent(percent);
      case SessionShutdownCommand():
        _shutdown();
    }
  }

  void _startConnect(SessionConnectCommand cmd) {
    _desiredConnected = true;
    if (_sessionId != 0 && _relay.sessionValid(_sessionId)) {
      if (!_relay.sessionReady(_sessionId)) {
        _ensureConnectLoop();
      } else {
        _publishState(connected: true, connecting: false);
      }
      return;
    }
    if (_sessionId != 0) {
      _relay.disconnect(_sessionId);
      _sessionId = 0;
    }
    final id = _relay.prepare(
      host: cmd.host,
      port: cmd.port,
      channel: cmd.channel,
      repeater: cmd.repeater,
    );
    if (id == 0) {
      _desiredConnected = false;
      _publishState(connected: false, connecting: false, error: 'prepare failed');
      return;
    }
    _sessionId = id;
    _publishState(connected: false, connecting: true);
    _ensureConnectLoop();
  }

  void _ensureConnectLoop() {
    if (_connectLoopRunning) {
      return;
    }
    _connectLoopRunning = true;
    unawaited(_connectLoop());
  }

  Future<void> _connectLoop() async {
    while (_desiredConnected && _sessionId != 0 && _relay.sessionValid(_sessionId)) {
      if (_relay.sessionReady(_sessionId)) {
        _publishState(connected: true, connecting: false);
        break;
      }
      _relay.connect(_sessionId, timeoutMs: 3500);
      await Future<void>.delayed(const Duration(milliseconds: 400));
      if (_relay.sessionReady(_sessionId)) {
        _publishState(connected: true, connecting: false);
        break;
      }
      await Future<void>.delayed(const Duration(milliseconds: 600));
    }
    _connectLoopRunning = false;
    if (!_desiredConnected) {
      _publishState(connected: false, connecting: false);
    }
  }

  void _disconnect() {
    _desiredConnected = false;
    final id = _sessionId;
    if (id != 0) {
      _relay.pttUp(id);
      _relay.disconnect(id);
    }
    _sessionId = 0;
    _publishState(connected: false, connecting: false);
  }

  void _pttDown() {
    if (_sessionId == 0) {
      return;
    }
    final rc = _relay.pttDown(_sessionId);
    _mainPort.send(SessionWorkerMessage.pttResult(
      active: rc == 0,
      resultCode: rc,
    ));
  }

  void _pttUp() {
    if (_sessionId == 0) {
      return;
    }
    final rc = _relay.pttUp(_sessionId);
    _mainPort.send(SessionWorkerMessage.pttResult(
      active: false,
      resultCode: rc,
    ));
  }

  void _drainNativeEvents() {
    while (true) {
      final ev = _relay.pollEvent();
      if (ev == null) {
        break;
      }
      _mainPort.send(SessionWorkerMessage.nativeEvent(
        eventType: ev.eventType,
        sessionId: ev.sessionId,
        info: ev.info,
      ));
      switch (ev.eventType) {
        case OwalkieEventType.connected:
          _publishState(connected: true, connecting: false);
        case OwalkieEventType.connectionLost:
          _publishState(connected: false, connecting: true);
          if (_desiredConnected) {
            _ensureConnectLoop();
          }
        case OwalkieEventType.disconnected:
        case OwalkieEventType.connectionFailed:
        case OwalkieEventType.protocolError:
          if (!_desiredConnected) {
            _publishState(connected: false, connecting: false);
          }
      }
    }
  }

  void _publishState({
    required bool connected,
    required bool connecting,
    String? error,
  }) {
    _mainPort.send(SessionWorkerMessage.transportState(
      sessionId: _sessionId,
      connected: connected,
      connecting: connecting,
      error: error,
    ));
  }

  void _shutdown() {
    _desiredConnected = false;
    _pollTimer?.cancel();
    _relay.disconnectAll();
    _relay.shutdown();
    _commands.close();
    Isolate.exit();
  }
}
