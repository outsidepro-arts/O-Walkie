/// Commands main isolate → session worker.
sealed class SessionCommand {
  const SessionCommand();
}

final class SessionConnectCommand extends SessionCommand {
  const SessionConnectCommand({
    required this.host,
    required this.port,
    required this.channel,
    this.repeater = false,
  });

  final String host;
  final int port;
  final String channel;
  final bool repeater;
}

final class SessionDisconnectCommand extends SessionCommand {
  const SessionDisconnectCommand();
}

final class SessionPttDownCommand extends SessionCommand {
  const SessionPttDownCommand();
}

final class SessionPttUpCommand extends SessionCommand {
  const SessionPttUpCommand();
}

final class SessionSetRxVolumeCommand extends SessionCommand {
  const SessionSetRxVolumeCommand(this.percent);

  final int percent;
}

final class SessionShutdownCommand extends SessionCommand {
  const SessionShutdownCommand();
}

/// Messages session worker → main isolate.
sealed class SessionWorkerMessage {
  const SessionWorkerMessage();

  const factory SessionWorkerMessage.sessionUnsupported() = SessionUnsupportedMessage;

  const factory SessionWorkerMessage.coreInfo({
    required String version,
    required int protocolVersion,
  }) = SessionCoreInfoMessage;

  const factory SessionWorkerMessage.loadFailed(String message) = SessionLoadFailedMessage;

  const factory SessionWorkerMessage.transportState({
    required int sessionId,
    required bool connected,
    required bool connecting,
    String? error,
  }) = SessionTransportStateMessage;

  const factory SessionWorkerMessage.nativeEvent({
    required int eventType,
    required int sessionId,
    required String info,
  }) = SessionNativeEventMessage;

  const factory SessionWorkerMessage.pttResult({
    required bool active,
    required int resultCode,
  }) = SessionPttResultMessage;
}

final class SessionUnsupportedMessage extends SessionWorkerMessage {
  const SessionUnsupportedMessage();
}

final class SessionCoreInfoMessage extends SessionWorkerMessage {
  const SessionCoreInfoMessage({
    required this.version,
    required this.protocolVersion,
  });

  final String version;
  final int protocolVersion;
}

final class SessionLoadFailedMessage extends SessionWorkerMessage {
  const SessionLoadFailedMessage(this.message);

  final String message;
}

final class SessionTransportStateMessage extends SessionWorkerMessage {
  const SessionTransportStateMessage({
    required this.sessionId,
    required this.connected,
    required this.connecting,
    this.error,
  });

  final int sessionId;
  final bool connected;
  final bool connecting;
  final String? error;
}

final class SessionNativeEventMessage extends SessionWorkerMessage {
  const SessionNativeEventMessage({
    required this.eventType,
    required this.sessionId,
    required this.info,
  });

  final int eventType;
  final int sessionId;
  final String info;
}

final class SessionPttResultMessage extends SessionWorkerMessage {
  const SessionPttResultMessage({
    required this.active,
    required this.resultCode,
  });

  final bool active;
  final int resultCode;
}
