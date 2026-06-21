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

@pragma('vm:entry-point')
void owalkieSessionWorkerEntry(List<dynamic> args) {
  final mainSendPort = args[0] as SendPort;
  final worker = _SessionWorker(mainSendPort);
  worker.run();
}

enum SessionState {
  idle,
  connecting,
  connected,
  paused,
}

class _SessionWorker {
  _SessionWorker(this._mainPort);

  final SendPort _mainPort;
  late final SessionRelayBindings _relay;
  final ReceivePort _commands = ReceivePort();

  int _sessionId = 0;
  SessionState _state = SessionState.idle;
  bool _warmMicRecorderEnabled = false;
  bool _appInForeground = true;
  bool _localTxActive = false;
  SessionConnectCommand? _lastConnect;
  Timer? _pollTimer;
  Timer? _reconnectTimer;
  Timer? _settleTimer;
  int _reconnectAttempt = 0;
  int _seriesPauseMs = 3000;
  int _lastRecoverAtMs = 0;
  int _lastConnectedAtMs = 0;
  bool _pendingNetworkRecover = false;
  List<int> _pttReleasePcm = const [];

  static const _attemptsPerSeries = 4;
  static const _intervalInSeriesMs = 300;
  static const _initialSeriesPauseMs = 3000;
  static const _maxSeriesPauseMs = 10000;
  static const _seriesPauseMultiplier = 1.5;
  static const _networkRecoverMinIntervalMs = 3000;
  static const _reconnectSettleMs = 2500;
  static const _settlePollIntervalMs = 100;
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
    if (message is! SessionCommand) return;
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
      case SessionSoundBankCommand(:final pttRelease):
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
      case SessionSyncWarmCaptureCommand(:final warmMicEnabled, :final appInForeground):
        _warmMicRecorderEnabled = warmMicEnabled;
        _appInForeground = appInForeground;
        _applyWarmCaptureFromFlags();
      case SessionSetAndroidBtVoiceRouteCommand(:final enabled):
        _relay.setAndroidBtVoiceRoute(enabled);
    }
  }

  void _applyWarmCaptureFromFlags() {
    final shouldOffer = _warmMicRecorderEnabled &&
        _appInForeground &&
        (_state == SessionState.connected ||
            _state == SessionState.connecting) &&
        !_localTxActive &&
        _sessionId != 0;
    if (shouldOffer) {
      _relay.warmCapture();
    } else {
      _relay.releaseCaptureIfIdle();
    }
  }

  void _scheduleReconnect() {
    _reconnectTimer?.cancel();
    _publishState();
    _reconnectAttempt = 0;
    _seriesPauseMs = _initialSeriesPauseMs;
    _scheduleReconnectTick();
  }

  void _scheduleReconnectTick() {
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer.periodic(
      const Duration(milliseconds: _intervalInSeriesMs),
      (_) => _onReconnectTick(),
    );
  }

  void _onReconnectTick() {
    if (_state != SessionState.connecting) {
      _reconnectTimer?.cancel();
      _reconnectTimer = null;
      return;
    }
    if (_sessionId == 0 || !_relay.sessionValid(_sessionId)) {
      return;
    }
    if (_relay.sessionReady(_sessionId)) {
      _setState(SessionState.connected);
      _publishState();
      _relay.punchNat(_sessionId);
      _reconnectTimer?.cancel();
      _reconnectTimer = null;
      return;
    }
    _reconnectAttempt++;
    if (_reconnectAttempt > _attemptsPerSeries) {
      _reconnectAttempt = 0;
      _reconnectTimer?.cancel();
      _reconnectTimer = Timer(Duration(milliseconds: _seriesPauseMs), () {
        _seriesPauseMs = math.min(
          (_seriesPauseMs * _seriesPauseMultiplier).round(),
          _maxSeriesPauseMs,
        );
        _scheduleReconnectTick();
      });
      return;
    }
    _relay.connectCancel(_sessionId);
    _relay.connectAsync(_sessionId, timeoutMs: 3500);
  }

  void _cancelSettle() {
    _settleTimer?.cancel();
    _settleTimer = null;
  }

  void _enterSettleCheck() {
    _cancelSettle();
    _cancelReconnect();
    final deadline = DateTime.now().millisecondsSinceEpoch + _reconnectSettleMs;
    _settleTimer = Timer.periodic(
      const Duration(milliseconds: _settlePollIntervalMs),
      (_) {
        if (_state != SessionState.connected) {
          _cancelSettle();
          return;
        }
        if (_relay.sessionReady(_sessionId)) {
          _cancelSettle();
          _relay.punchNat(_sessionId);
          _publishState();
          return;
        }
        if (DateTime.now().millisecondsSinceEpoch >= deadline) {
          _cancelSettle();
          _setState(SessionState.connecting);
          _publishState();
          _scheduleReconnect();
        }
      },
    );
  }

  void _cancelReconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _reconnectAttempt = 0;
    _seriesPauseMs = _initialSeriesPauseMs;
    if (_sessionId != 0) {
      _relay.connectCancel(_sessionId);
    }
  }

  void _setState(SessionState newState) {
    if (_state == newState) return;
    _state = newState;
    if (newState == SessionState.idle) {
      _cancelReconnect();
    }
  }

  void _publishState({String? error}) {
    final connected = _state == SessionState.connected;
    final connecting = _state == SessionState.connecting || _state == SessionState.paused;
    final reconnecting = _state == SessionState.connecting;
    _mainPort.send(SessionWorkerMessage.transportState(
      sessionId: _sessionId,
      connected: connected,
      connecting: connecting,
      reconnecting: reconnecting,
      error: error,
    ));
    _applyWarmCaptureFromFlags();
  }

  void _pauseRelayForExternalReason() {
    if (_state == SessionState.paused) return;
    if (_state != SessionState.connected && _state != SessionState.connecting) return;
    _cancelReconnect();
    final id = _sessionId;
    if (id != 0) {
      _relay.pttUp(id);
      _relay.disconnect(id);
      _sessionId = 0;
    }
    _localTxActive = false;
    _pendingNetworkRecover = false;
    _setState(SessionState.paused);
    _publishState();
  }

  void _resumeRelayAfterExternalPause() {
    if (_state != SessionState.paused) return;
    _setState(SessionState.idle);
    final cmd = _lastConnect;
    if (cmd == null) return;
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
      _publishState(error: 'prepare failed');
      return;
    }
    _sessionId = id;
    _setState(SessionState.connecting);
    _publishState();
    _scheduleReconnect();
  }

  void _punchNat() {
    if (_sessionId == 0) return;
    _relay.punchNat(_sessionId);
  }

  void _publishUplinkSignal() {
    final percent = LinkSignal.byteToPercent(_relay.getUplinkSignalByte());
    _mainPort.send(SessionWorkerMessage.uplinkSignal(percent: percent));
  }

  void _recoverAfterNetworkHandoff() {
    if (_state == SessionState.idle || _sessionId == 0) return;
    if (_localTxActive) {
      _pendingNetworkRecover = true;
      return;
    }
    _pendingNetworkRecover = false;
    if (_relay.sessionReady(_sessionId)) {
      final now = DateTime.now().millisecondsSinceEpoch;
      if (now - _lastRecoverAtMs < _networkRecoverMinIntervalMs) return;
      _lastRecoverAtMs = now;
      _relay.recoverUdp(_sessionId);
      _relay.punchNat(_sessionId);
      return;
    }
    if (_state == SessionState.connected) {
      _setState(SessionState.connecting);
      _publishState();
    }
    _scheduleReconnect();
  }

  void _drainPendingNetworkRecover() {
    if (!_pendingNetworkRecover || _localTxActive) return;
    _recoverAfterNetworkHandoff();
  }

  void _startConnect(SessionConnectCommand cmd) {
    _lastConnect = cmd;
    if (_sessionId != 0 && _relay.sessionValid(_sessionId)) {
      if (_relay.sessionReady(_sessionId)) {
        _setState(SessionState.connected);
        _publishState();
        return;
      }
      _setState(SessionState.connecting);
      _publishState();
      _scheduleReconnect();
      return;
    }
    if (_sessionId != 0) {
      _relay.pttUp(_sessionId);
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
      _setState(SessionState.idle);
      _publishState(error: 'prepare failed');
      return;
    }
    _sessionId = id;
    _setState(SessionState.connecting);
    _publishState();
    _scheduleReconnect();
  }

  void _switchServer(SessionSwitchServerCommand cmd) {
    _lastConnect = SessionConnectCommand(
      host: cmd.host,
      port: cmd.port,
      channel: cmd.channel,
      repeater: cmd.repeater,
    );
    _cancelReconnect();
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
      _setState(SessionState.idle);
      _publishState(error: 'prepare failed');
      return;
    }
    _sessionId = newId;
    _setState(SessionState.connecting);
    _publishState();
    _scheduleReconnect();
  }

  void _disconnect() {
    _cancelReconnect();
    _localTxActive = false;
    _pendingNetworkRecover = false;
    final id = _sessionId;
    if (id != 0) {
      _relay.pttUp(id);
      _relay.disconnect(id);
    }
    _sessionId = 0;
    _relay.releaseSessionAudio();
    _setState(SessionState.idle);
    _publishState();
    _mainPort.send(const SessionWorkerMessage.disconnectComplete());
  }

  void _setRepeater(bool enabled) {
    if (_sessionId == 0) return;
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
    if (_sessionId == 0) return;
    final rc = _relay.pttDown(_sessionId);
    _localTxActive = rc == 0;
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
    if (reps <= 1) return base;
    return [for (var i = 0; i < reps; i++) ...base];
  }

  void _pttUp(List<({double freqHz, int durationMs})> rogerRaw) {
    if (_sessionId == 0) return;
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
    } else if (_pttReleasePcm.isNotEmpty) {
      local = Int16List(_pttReleasePcm.length);
      for (var i = 0; i < _pttReleasePcm.length; i++) {
        local[i] = _pttReleasePcm[i];
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
    _applyWarmCaptureFromFlags();
    _drainPendingNetworkRecover();
  }

  void _playLocalUi(List<int> samples, int sampleRate) {
    if (samples.isEmpty) return;
    final pcm = Int16List(samples.length);
    for (var i = 0; i < samples.length; i++) {
      pcm[i] = samples[i];
    }
    _relay.playLocalPcmAsync(pcm, sampleRate: sampleRate);
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
      if (ev == null) break;
      _mainPort.send(SessionWorkerMessage.nativeEvent(
        eventType: ev.eventType,
        sessionId: ev.sessionId,
        info: ev.info,
      ));
      switch (ev.eventType) {
        case OwalkieEventType.connected:
          _lastConnectedAtMs = DateTime.now().millisecondsSinceEpoch;
          _relay.punchNat(_sessionId);
          if (_state == SessionState.connected) break;
          _cancelSettle();
          _setState(SessionState.connected);
          _publishState();
        case OwalkieEventType.connectionLost:
          if (_state == SessionState.connected) {
            final elapsed = DateTime.now().millisecondsSinceEpoch - _lastConnectedAtMs;
            if (_lastConnectedAtMs > 0 && elapsed < _reconnectSettleMs) {
              _enterSettleCheck();
            } else {
              _setState(SessionState.connecting);
              _publishState();
              _scheduleReconnect();
            }
          }
        case OwalkieEventType.protocolError:
          _cancelReconnect();
          _setState(SessionState.idle);
          _publishState(error: ev.info.isNotEmpty ? ev.info : 'Protocol error');
        case OwalkieEventType.connectionFailed:
          if (_state == SessionState.connecting) {
            _publishState(error: ev.info.isNotEmpty ? ev.info : null);
          }
        case OwalkieEventType.disconnected:
          if (_state == SessionState.idle) break;
          _cancelReconnect();
          _setState(SessionState.idle);
          _publishState(error: ev.info.isNotEmpty ? ev.info : null);
      }
    }
  }

  void _shutdown() {
    _cancelSettle();
    _cancelReconnect();
    _pollTimer?.cancel();
    _pollTimer = null;
    _relay.disconnectAll();
    _relay.shutdown();
    _commands.close();
    _mainPort.send(const SessionWorkerMessage.shutdownComplete());
    Isolate.exit();
  }
}
