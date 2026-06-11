import 'dart:async';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import '../owalkie_core_bindings_generated.dart';
import 'native_library.dart';
import 'link_signal.dart';
import 'session_event_type.dart';
import 'session_messages.dart';
import 'session_relay_bindings.dart';
import 'signal_point.dart';

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
  bool _clientReconnectRunning = false;
  bool _hadConnected = false;
  bool _localTxActive = false;
  bool _pendingNetworkRecover = false;
  bool _relayPausedExternally = false;
  SessionConnectCommand? _lastConnect;
  int _lastRecoverAtMs = 0;
  bool _publishedConnected = false;
  bool _publishedConnecting = false;
  bool _publishedReconnecting = false;
  Timer? _pollTimer;
  List<int> _pttPressPcm = const [];
  List<int> _pttReleasePcm = const [];

  static const _initialBackoffMs = 1500;
  static const _maxBackoffMs = 8000;
  static const _networkRecoverMinIntervalMs = 3000;
  static const _localPlaybackRate = 44100;
  static const _rogerTailMs = 40;
  static const _callLocalGain = 0.316;

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
      case SessionSwitchServerCommand():
        _switchServer(message);
      case SessionDisconnectCommand():
        _disconnect();
      case SessionPttDownCommand():
        _pttDown();
      case SessionPttUpCommand(:final rogerPoints):
        _pttUp(rogerPoints);
      case SessionSendCallCommand(:final points, :final repeatCount):
        unawaited(_sendCall(points, repeatCount));
      case SessionPlayLocalCommand(:final samples, :final sampleRate):
        _playLocalUi(samples, sampleRate);
      case SessionSoundBankCommand(:final pttPress, :final pttRelease):
        _pttPressPcm = pttPress;
        _pttReleasePcm = pttRelease;
      case SessionSetRxVolumeCommand(:final percent):
        _relay.setRxVolumePercent(percent);
      case SessionSetRepeaterCommand(:final enabled):
        _setRepeater(enabled);
      case SessionCheckChannelActivityCommand():
        _checkChannelActivity(message);
      case SessionShutdownCommand():
        _shutdown();
      case SessionPunchNatCommand():
        _punchNat();
      case SessionReportSignalCommand(:final mode, :final value):
        _relay.reportSignal(mode: mode, value: value);
        _publishUplinkSignal();
      case SessionClearSignalCommand(:final mode):
        _relay.clearSignal(mode);
        _publishUplinkSignal();
      case SessionBindProcessNetworkCommand(:final networkHandle):
        _relay.bindProcessNetwork(networkHandle);
      case SessionNetworkHandoffCommand():
        _recoverAfterNetworkHandoff();
      case SessionPauseRelayCommand():
        _pauseRelayForExternalReason();
      case SessionResumeRelayCommand():
        _resumeRelayAfterExternalPause();
    }
  }

  void _pauseRelayForExternalReason() {
    if (_relayPausedExternally || !_desiredConnected) {
      return;
    }
    _relayPausedExternally = true;
    final id = _sessionId;
    if (id != 0) {
      _relay.pttUp(id);
      _relay.disconnect(id);
      _sessionId = 0;
    }
    _localTxActive = false;
    _pendingNetworkRecover = false;
    _publishState(connected: false, connecting: false);
  }

  void _resumeRelayAfterExternalPause() {
    if (!_relayPausedExternally) {
      return;
    }
    _relayPausedExternally = false;
    if (!_desiredConnected) {
      return;
    }
    final cmd = _lastConnect;
    if (cmd == null) {
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
      _publishState(connected: false, connecting: false, error: 'prepare failed');
      return;
    }
    _sessionId = id;
    _publishState(connected: false, connecting: true);
    _ensureClientReconnectLoop();
  }

  void _punchNat() {
    if (_sessionId == 0) {
      return;
    }
    _relay.punchNat(_sessionId);
  }

  void _publishUplinkSignal() {
    final percent = LinkSignal.byteToPercent(_relay.getUplinkSignalByte());
    _mainPort.send(SessionWorkerMessage.uplinkSignal(percent: percent));
  }

  void _recoverAfterNetworkHandoff() {
    if (!_desiredConnected || _sessionId == 0) {
      return;
    }
    if (_localTxActive) {
      _pendingNetworkRecover = true;
      return;
    }
    _pendingNetworkRecover = false;
    if (_relay.sessionReady(_sessionId)) {
      final now = DateTime.now().millisecondsSinceEpoch;
      if (now - _lastRecoverAtMs < _networkRecoverMinIntervalMs) {
        return;
      }
      _lastRecoverAtMs = now;
      _relay.recoverUdp(_sessionId);
      _relay.punchNat(_sessionId);
      return;
    }
    _ensureClientReconnectLoop();
  }

  void _drainPendingNetworkRecover() {
    if (!_pendingNetworkRecover || _localTxActive) {
      return;
    }
    _recoverAfterNetworkHandoff();
  }

  void _startConnect(SessionConnectCommand cmd) {
    _lastConnect = cmd;
    _desiredConnected = true;
    _relayPausedExternally = false;
    if (_sessionId != 0 && _relay.sessionValid(_sessionId)) {
      if (_relay.sessionReady(_sessionId)) {
        _publishState(connected: true, connecting: false);
      }
      _ensureClientReconnectLoop();
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
    _ensureClientReconnectLoop();
  }

  void _switchServer(SessionSwitchServerCommand cmd) {
    _lastConnect = SessionConnectCommand(
      host: cmd.host,
      port: cmd.port,
      channel: cmd.channel,
      repeater: cmd.repeater,
    );
    _desiredConnected = true;
    _relayPausedExternally = false;
    _localTxActive = false;
    _pendingNetworkRecover = false;
    final id = _sessionId;
    if (id != 0) {
      _relay.pttUp(id);
      _relay.disconnect(id);
    }
    _sessionId = 0;
    final newId = _relay.prepare(
      host: cmd.host,
      port: cmd.port,
      channel: cmd.channel,
      repeater: cmd.repeater,
    );
    if (newId == 0) {
      _publishState(connected: false, connecting: false, error: 'prepare failed');
      return;
    }
    _sessionId = newId;
    _publishState(connected: false, connecting: true, reconnecting: false);
    _ensureClientReconnectLoop();
  }

  void _ensureClientReconnectLoop() {
    if (_clientReconnectRunning) {
      return;
    }
    _clientReconnectRunning = true;
    unawaited(_clientReconnectLoop());
  }

  Future<void> _clientReconnectLoop() async {
    var backoffMs = _initialBackoffMs;
    while (_desiredConnected && !_relayPausedExternally) {
      if (_sessionId == 0 || !_relay.sessionValid(_sessionId)) {
        await Future<void>.delayed(const Duration(milliseconds: 500));
        continue;
      }
      if (_relay.sessionReady(_sessionId)) {
        backoffMs = _initialBackoffMs;
        if (!_publishedConnected || _publishedConnecting || _publishedReconnecting) {
          _publishState(connected: true, connecting: false, reconnecting: false);
        }
        await Future<void>.delayed(const Duration(milliseconds: 500));
        continue;
      }
      _publishState(
        connected: false,
        connecting: true,
        reconnecting: _hadConnected,
      );
      _relay.connect(_sessionId, timeoutMs: 3500);
      await Future<void>.delayed(const Duration(milliseconds: 400));
      if (_relay.sessionReady(_sessionId)) {
        backoffMs = _initialBackoffMs;
        _publishState(connected: true, connecting: false, reconnecting: false);
        continue;
      }
      await Future<void>.delayed(Duration(milliseconds: backoffMs));
      backoffMs = math.min((backoffMs * 1.5).round(), _maxBackoffMs);
    }
    _clientReconnectRunning = false;
    if (!_desiredConnected) {
      _publishState(connected: false, connecting: false);
    }
  }

  void _disconnect() {
    _desiredConnected = false;
    _hadConnected = false;
    _relayPausedExternally = false;
    _localTxActive = false;
    _pendingNetworkRecover = false;
    final id = _sessionId;
    if (id != 0) {
      _relay.pttUp(id);
      _relay.disconnect(id);
    }
    _sessionId = 0;
    _publishState(connected: false, connecting: false);
  }

  void _setRepeater(bool enabled) {
    if (_sessionId == 0) {
      return;
    }
    _relay.setRepeaterMode(_sessionId, enabled: enabled);
  }

  void _checkChannelActivity(SessionCheckChannelActivityCommand cmd) {
    final result = _relay.checkChannelActivity(
      host: cmd.host,
      port: cmd.port,
      channel: cmd.channel,
      timeoutMs: cmd.timeoutMs,
    );
    _mainPort.send(SessionWorkerMessage.channelActivityResult(
      requestId: cmd.requestId,
      resultCode: result.resultCode,
      active: result.active,
    ));
  }

  void _pttDown() {
    if (_sessionId == 0) {
      return;
    }
    final rc = _relay.pttDown(_sessionId);
    _localTxActive = rc == 0;
    if (_localTxActive && _pttPressPcm.isNotEmpty) {
      _playLocalUi(_pttPressPcm, _localPlaybackRate);
    }
    _mainPort.send(SessionWorkerMessage.pttResult(
      active: _localTxActive,
      resultCode: rc,
    ));
  }

  List<SignalPoint> _toSignalPoints(
    List<({double freqHz, int durationMs})> raw,
  ) {
    return [
      for (final p in raw)
        SignalPoint(freqHz: p.freqHz, durationMs: p.durationMs),
    ];
  }

  List<SignalPoint> _expandedCallPoints(
    List<({double freqHz, int durationMs})> raw,
    int repeatCount,
  ) {
    final base = _toSignalPoints(raw);
    final reps = repeatCount.clamp(1, 500);
    if (reps <= 1) {
      return base;
    }
    return [for (var i = 0; i < reps; i++) ...base];
  }

  void _pttUp(List<({double freqHz, int durationMs})> rogerRaw) {
    if (_sessionId == 0) {
      return;
    }
    final rogerPoints = _toSignalPoints(rogerRaw);
    Int16List? uplink;
    Int16List? local;
    if (rogerPoints.isNotEmpty) {
      final codecRate = _relay.codecSampleRate;
      uplink = _relay.generateSignalPcm(
        points: rogerPoints,
        sampleRate: codecRate,
        tailMs: _rogerTailMs,
      );
      local = _relay.generateSignalPcm(
        points: rogerPoints,
        sampleRate: _localPlaybackRate,
        tailMs: _rogerTailMs,
      );
      if (local != null && _pttReleasePcm.isNotEmpty) {
        final merged = Int16List(_pttReleasePcm.length + local.length);
        for (var i = 0; i < _pttReleasePcm.length; i++) {
          merged[i] = _pttReleasePcm[i];
        }
        merged.setRange(_pttReleasePcm.length, merged.length, local);
        local = merged;
      }
    }
    final rc = _relay.pttUpWithRoger(
      sessionId: _sessionId,
      rogerUplink: uplink,
      rogerLocal: local,
      localSampleRate: _localPlaybackRate,
    );
    _localTxActive = false;
    _mainPort.send(SessionWorkerMessage.pttResult(
      active: false,
      resultCode: rc,
    ));
    _drainPendingNetworkRecover();
  }

  void _playLocalUi(List<int> samples, int sampleRate) {
    if (samples.isEmpty) {
      return;
    }
    final pcm = Int16List(samples.length);
    for (var i = 0; i < samples.length; i++) {
      pcm[i] = samples[i];
    }
    _relay.playLocalPcm(pcm, sampleRate: sampleRate);
  }

  Future<void> _sendCall(
    List<({double freqHz, int durationMs})> raw,
    int repeatCount,
  ) async {
    if (_sessionId == 0) {
      _mainPort.send(const SessionWorkerMessage.callResult(resultCode: -1));
      return;
    }
    final points = _expandedCallPoints(raw, repeatCount);
    if (points.isEmpty) {
      _mainPort.send(const SessionWorkerMessage.callResult(resultCode: -1));
      return;
    }
    final codecRate = _relay.codecSampleRate;
    final uplink = _relay.generateSignalPcm(
      points: points,
      sampleRate: codecRate,
    );
    final local = _relay.generateSignalPcm(
      points: points,
      sampleRate: _localPlaybackRate,
      gain: _callLocalGain,
    );
    if (uplink == null) {
      _mainPort.send(const SessionWorkerMessage.callResult(resultCode: -1));
      return;
    }
    final rc = _relay.sendCallSignal(
      sessionId: _sessionId,
      uplink: uplink,
      local: local,
      localSampleRate: _localPlaybackRate,
    );
    _mainPort.send(SessionWorkerMessage.callResult(resultCode: rc));
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
          _hadConnected = true;
          _publishState(connected: true, connecting: false, clearError: true);
          _relay.punchNat(_sessionId);
        case OwalkieEventType.connectionLost:
          _publishState(connected: false, connecting: true, reconnecting: true);
          if (_desiredConnected) {
            _ensureClientReconnectLoop();
          }
        case OwalkieEventType.protocolError:
          _desiredConnected = false;
          _hadConnected = false;
          _publishState(
            connected: false,
            connecting: false,
            error: ev.info.isNotEmpty ? ev.info : 'Protocol error',
          );
        case OwalkieEventType.connectionFailed:
          if (_desiredConnected && _hadConnected) {
            _publishState(
              connected: false,
              connecting: true,
              reconnecting: true,
              error: ev.info.isNotEmpty ? ev.info : null,
            );
          } else if (!_desiredConnected) {
            _publishState(connected: false, connecting: false);
          } else {
            _publishState(
              connected: false,
              connecting: true,
              error: ev.info.isNotEmpty ? ev.info : null,
            );
          }
        case OwalkieEventType.disconnected:
          if (!_desiredConnected) {
            _hadConnected = false;
            _publishState(
              connected: false,
              connecting: false,
              error: ev.info.isNotEmpty ? ev.info : null,
            );
          }
      }
    }
  }

  void _publishState({
    required bool connected,
    required bool connecting,
    bool reconnecting = false,
    String? error,
    bool clearError = false,
  }) {
    if (error == null &&
        !clearError &&
        connected == _publishedConnected &&
        connecting == _publishedConnecting &&
        reconnecting == _publishedReconnecting) {
      return;
    }
    _publishedConnected = connected;
    _publishedConnecting = connecting;
    _publishedReconnecting = reconnecting;
    _mainPort.send(SessionWorkerMessage.transportState(
      sessionId: _sessionId,
      connected: connected,
      connecting: connecting,
      reconnecting: reconnecting,
      error: clearError ? null : error,
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
