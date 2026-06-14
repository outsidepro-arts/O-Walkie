import 'dart:collection';

class SessionTelemetry {
  static const _maxEvents = 100;
  final Queue<_TelemetryEvent> _events = Queue<_TelemetryEvent>();

  void log(String event, [String? details]) {
    final entry = _TelemetryEvent(
      timestamp: DateTime.now(),
      event: event,
      details: details,
    );
    _events.addLast(entry);
    if (_events.length > _maxEvents) {
      _events.removeFirst();
    }
  }

  void connect({required String host, required int port, required String channel}) {
    log('CONNECT', '$host:$port/$channel');
  }

  void disconnect({String? reason}) {
    log('DISCONNECT', reason);
  }

  void connected() {
    log('CONNECTED');
  }

  void connectionLost() {
    log('CONNECTION_LOST');
  }

  void reconnectAttempt(int attemptNumber) {
    log('RECONNECT_ATTEMPT', 'attempt #$attemptNumber');
  }

  void pttDown() {
    log('PTT_DOWN');
  }

  void pttUp({required bool success}) {
    log('PTT_UP', success ? 'success' : 'failed');
  }

  void scanStart() {
    log('SCAN_START');
  }

  void scanStop() {
    log('SCAN_STOP');
  }

  void scanFound(String profileName) {
    log('SCAN_FOUND', profileName);
  }

  void error(String message) {
    log('ERROR', message);
  }

  List<String> dump() {
    return _events.map((e) => e.toString()).toList();
  }

  void clear() {
    _events.clear();
  }
}

class _TelemetryEvent {
  _TelemetryEvent({
    required this.timestamp,
    required this.event,
    this.details,
  });

  final DateTime timestamp;
  final String event;
  final String? details;

  @override
  String toString() {
    final time = timestamp.toIso8601String().substring(11, 23);
    final detail = details != null ? ' $details' : '';
    return '[$time] $event$detail';
  }
}
