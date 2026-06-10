import 'dart:ffi' as ffi;
import 'package:ffi/ffi.dart';

import 'native_library.dart';

final class OwalkiePolledEvent extends ffi.Struct {
  @ffi.Int32()
  external int eventType;

  @ffi.Int64()
  external int sessionId;

  @ffi.Array(512)
  external ffi.Array<ffi.Char> info;
}

typedef _HasSessionNative = ffi.Int32 Function();
typedef _HasSession = int Function();

typedef _ShutdownNative = ffi.Void Function();
typedef _Shutdown = void Function();

typedef _PrepareNative = ffi.Int64 Function(
  ffi.Pointer<ffi.Char> host,
  ffi.Int32 port,
  ffi.Pointer<ffi.Char> channel,
  ffi.Int32 repeater,
);
typedef _Prepare = int Function(
  ffi.Pointer<ffi.Char> host,
  int port,
  ffi.Pointer<ffi.Char> channel,
  int repeater,
);

typedef _ConnectNative = ffi.Int32 Function(ffi.Int64 sessionId, ffi.Int32 timeoutMs);
typedef _Connect = int Function(int sessionId, int timeoutMs);

typedef _DisconnectNative = ffi.Void Function(ffi.Int64 sessionId);
typedef _Disconnect = void Function(int sessionId);

typedef _DisconnectAllNative = ffi.Void Function();
typedef _DisconnectAll = void Function();

typedef _SessionFlagNative = ffi.Int32 Function(ffi.Int64 sessionId);
typedef _SessionFlag = int Function(int sessionId);

typedef _SetRxVolumeNative = ffi.Void Function(ffi.Int32 percent);
typedef _SetRxVolume = void Function(int percent);

typedef _PttNative = ffi.Int32 Function(ffi.Int64 sessionId);
typedef _Ptt = int Function(int sessionId);

typedef _PollEventNative = ffi.Int32 Function(ffi.Pointer<OwalkiePolledEvent> out);
typedef _PollEvent = int Function(ffi.Pointer<OwalkiePolledEvent> out);

/// Low-level FFI to owalkie_flutter_* session exports (worker isolate only).
class SessionRelayBindings {
  SessionRelayBindings(this._lib);

  factory SessionRelayBindings.open() {
    return SessionRelayBindings(openOwalkieCoreLibrary());
  }

  final ffi.DynamicLibrary _lib;

  late final _HasSession _hasSession =
      _lib.lookupFunction<_HasSessionNative, _HasSession>('owalkie_flutter_has_session');

  late final _Shutdown _shutdown =
      _lib.lookupFunction<_ShutdownNative, _Shutdown>('owalkie_flutter_shutdown');

  late final _Prepare _prepare = _lib.lookupFunction<_PrepareNative, _Prepare>(
    'owalkie_flutter_prepare',
  );

  late final _Connect _connect = _lib.lookupFunction<_ConnectNative, _Connect>(
    'owalkie_flutter_connect',
  );

  late final _Disconnect _disconnect = _lib.lookupFunction<_DisconnectNative, _Disconnect>(
    'owalkie_flutter_disconnect',
  );

  late final _DisconnectAll _disconnectAll =
      _lib.lookupFunction<_DisconnectAllNative, _DisconnectAll>('owalkie_flutter_disconnect_all');

  late final _SessionFlag _sessionValid =
      _lib.lookupFunction<_SessionFlagNative, _SessionFlag>('owalkie_flutter_session_valid');

  late final _SessionFlag _sessionReady =
      _lib.lookupFunction<_SessionFlagNative, _SessionFlag>('owalkie_flutter_session_ready');

  late final _SetRxVolume _setRxVolume =
      _lib.lookupFunction<_SetRxVolumeNative, _SetRxVolume>('owalkie_flutter_set_rx_volume_percent');

  late final _Ptt _pttDown = _lib.lookupFunction<_PttNative, _Ptt>('owalkie_flutter_ptt_down');

  late final _Ptt _pttUp = _lib.lookupFunction<_PttNative, _Ptt>('owalkie_flutter_ptt_up');

  late final _PollEvent _pollEvent =
      _lib.lookupFunction<_PollEventNative, _PollEvent>('owalkie_flutter_poll_event');

  bool get hasSession => _hasSession() != 0;

  void shutdown() => _shutdown();

  int prepare({
    required String host,
    required int port,
    required String channel,
    bool repeater = false,
  }) {
    return using((arena) {
      final hostPtr = host.toNativeUtf8(allocator: arena);
      final channelPtr = channel.toNativeUtf8(allocator: arena);
      return _prepare(hostPtr.cast(), port, channelPtr.cast(), repeater ? 1 : 0);
    });
  }

  int connect(int sessionId, {int timeoutMs = 3500}) =>
      _connect(sessionId, timeoutMs);

  void disconnect(int sessionId) => _disconnect(sessionId);

  void disconnectAll() => _disconnectAll();

  bool sessionValid(int sessionId) => _sessionValid(sessionId) != 0;

  bool sessionReady(int sessionId) => _sessionReady(sessionId) != 0;

  void setRxVolumePercent(int percent) => _setRxVolume(percent);

  int pttDown(int sessionId) => _pttDown(sessionId);

  int pttUp(int sessionId) => _pttUp(sessionId);

  OwalkiePolledEventData? pollEvent() {
    final out = calloc<OwalkiePolledEvent>();
    try {
      if (_pollEvent(out) == 0) {
        return null;
      }
      final info = _readFixedCString(out.ref.info);
      return OwalkiePolledEventData(
        eventType: out.ref.eventType,
        sessionId: out.ref.sessionId,
        info: info,
      );
    } finally {
      calloc.free(out);
    }
  }
}

String _readFixedCString(ffi.Array<ffi.Char> chars) {
  final codes = <int>[];
  for (var i = 0; i < 512; i++) {
    final code = chars[i];
    if (code == 0) {
      break;
    }
    codes.add(code);
  }
  return String.fromCharCodes(codes);
}

class OwalkiePolledEventData {
  const OwalkiePolledEventData({
    required this.eventType,
    required this.sessionId,
    required this.info,
  });

  final int eventType;
  final int sessionId;
  final String info;
}
