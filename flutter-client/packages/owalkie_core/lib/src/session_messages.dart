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
  const SessionPttUpCommand({this.rogerPoints = const []});

  final List<({double freqHz, int durationMs})> rogerPoints;
}

final class SessionSendCallCommand extends SessionCommand {
  const SessionSendCallCommand({
    required this.points,
    this.repeatCount = 1,
  });

  final List<({double freqHz, int durationMs})> points;
  final int repeatCount;
}

final class SessionPlayLocalCommand extends SessionCommand {
  const SessionPlayLocalCommand({
    required this.samples,
    required this.sampleRate,
  });

  final List<int> samples;
  final int sampleRate;
}

final class SessionSetRxVolumeCommand extends SessionCommand {
  const SessionSetRxVolumeCommand(this.percent);

  final int percent;
}

final class SessionSetRepeaterCommand extends SessionCommand {
  const SessionSetRepeaterCommand(this.enabled);

  final bool enabled;
}

final class SessionCheckChannelActivityCommand extends SessionCommand {
  const SessionCheckChannelActivityCommand({
    required this.requestId,
    required this.host,
    required this.port,
    required this.channel,
    this.timeoutMs = 4000,
  });

  final int requestId;
  final String host;
  final int port;
  final String channel;
  final int timeoutMs;
}

final class SessionShutdownCommand extends SessionCommand {
  const SessionShutdownCommand();
}

final class SessionPunchNatCommand extends SessionCommand {
  const SessionPunchNatCommand();
}

final class SessionReportSignalCommand extends SessionCommand {
  const SessionReportSignalCommand({required this.mode, required this.value});

  final int mode;
  final int value;
}

final class SessionClearSignalCommand extends SessionCommand {
  const SessionClearSignalCommand(this.mode);

  final int mode;
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
    bool reconnecting,
    String? error,
  }) = SessionTransportStateMessage;

  const factory SessionWorkerMessage.channelActivityResult({
    required int requestId,
    required int resultCode,
    required bool active,
  }) = SessionChannelActivityResultMessage;

  const factory SessionWorkerMessage.nativeEvent({
    required int eventType,
    required int sessionId,
    required String info,
  }) = SessionNativeEventMessage;

  const factory SessionWorkerMessage.pttResult({
    required bool active,
    required int resultCode,
  }) = SessionPttResultMessage;

  const factory SessionWorkerMessage.callResult({
    required int resultCode,
  }) = SessionCallResultMessage;
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
    this.reconnecting = false,
    this.error,
  });

  final int sessionId;
  final bool connected;
  final bool connecting;
  final bool reconnecting;
  final String? error;
}

final class SessionChannelActivityResultMessage extends SessionWorkerMessage {
  const SessionChannelActivityResultMessage({
    required this.requestId,
    required this.resultCode,
    required this.active,
  });

  final int requestId;
  final int resultCode;
  final bool active;
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

final class SessionCallResultMessage extends SessionWorkerMessage {
  const SessionCallResultMessage({required this.resultCode});

  final int resultCode;
}
