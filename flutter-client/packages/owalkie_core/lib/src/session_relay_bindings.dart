import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'native_library.dart';
import 'signal_point.dart';

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

typedef _SetRepeaterNative = ffi.Int32 Function(ffi.Int64 sessionId, ffi.Int32 enabled);
typedef _SetRepeater = int Function(int sessionId, int enabled);

typedef _CheckActivityNative = ffi.Int32 Function(
  ffi.Pointer<ffi.Char> host,
  ffi.Int32 port,
  ffi.Pointer<ffi.Char> channel,
  ffi.Int32 timeoutMs,
  ffi.Pointer<ffi.Int32> outActive,
);
typedef _CheckActivity = int Function(
  ffi.Pointer<ffi.Char> host,
  int port,
  ffi.Pointer<ffi.Char> channel,
  int timeoutMs,
  ffi.Pointer<ffi.Int32> outActive,
);

final class OwalkieFlutterSignalSpec extends ffi.Struct {
  external ffi.Pointer<ffi.Double> freqHz;
  external ffi.Pointer<ffi.Int32> durationMs;

  @ffi.Uint64()
  external int pointCount;

  @ffi.Int32()
  external int tailMs;

  @ffi.Int32()
  external int repeatCount;

  @ffi.Double()
  external double gain;
}

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

  late final _SetRepeater _setRepeater = _lib.lookupFunction<_SetRepeaterNative, _SetRepeater>(
    'owalkie_flutter_set_repeater_mode',
  );

  late final _CheckActivity _checkActivity =
      _lib.lookupFunction<_CheckActivityNative, _CheckActivity>(
    'owalkie_flutter_check_channel_activity',
  );

  late final int Function() _codecSampleRate =
      _lib.lookupFunction<ffi.Int32 Function(), int Function()>(
    'owalkie_flutter_codec_sample_rate',
  );

  late final int Function() _codecFrameSamples =
      _lib.lookupFunction<ffi.Int32 Function(), int Function()>(
    'owalkie_flutter_codec_frame_samples',
  );

  late final _SignalGenerateNative = _lib.lookupFunction<
      ffi.Int32 Function(
        ffi.Pointer<OwalkieFlutterSignalSpec>,
        ffi.Int32,
        ffi.Pointer<ffi.Pointer<ffi.Int16>>,
        ffi.Pointer<ffi.Uint64>,
      ),
      int Function(
        ffi.Pointer<OwalkieFlutterSignalSpec>,
        int,
        ffi.Pointer<ffi.Pointer<ffi.Int16>>,
        ffi.Pointer<ffi.Uint64>,
      )>('owalkie_flutter_signal_generate');

  late final void Function(ffi.Pointer<ffi.Int16>) _signalFree =
      _lib.lookupFunction<ffi.Void Function(ffi.Pointer<ffi.Int16>), void Function(ffi.Pointer<ffi.Int16>)>(
    'owalkie_flutter_signal_free_pcm',
  );

  late final void Function(ffi.Pointer<ffi.Int16>, int, int) _playLocal =
      _lib.lookupFunction<ffi.Void Function(ffi.Pointer<ffi.Int16>, ffi.Uint64, ffi.Int32),
          void Function(ffi.Pointer<ffi.Int16>, int, int)>('owalkie_flutter_play_local_pcm');

  late final void Function(ffi.Pointer<ffi.Int16>, int, int) _startLocalLoop =
      _lib.lookupFunction<ffi.Void Function(ffi.Pointer<ffi.Int16>, ffi.Uint64, ffi.Int32),
          void Function(ffi.Pointer<ffi.Int16>, int, int)>('owalkie_flutter_start_local_pcm_loop');

  late final void Function() _stopLocalLoop =
      _lib.lookupFunction<ffi.Void Function(), void Function()>('owalkie_flutter_stop_local_pcm_loop');

  late final int Function(int, ffi.Pointer<ffi.Int16>, int, ffi.Pointer<ffi.Int16>, int, int)
      _pttUpWithRoger = _lib.lookupFunction<
          ffi.Int32 Function(
            ffi.Int64,
            ffi.Pointer<ffi.Int16>,
            ffi.Uint64,
            ffi.Pointer<ffi.Int16>,
            ffi.Uint64,
            ffi.Int32,
          ),
          int Function(int, ffi.Pointer<ffi.Int16>, int, ffi.Pointer<ffi.Int16>, int, int)>(
    'owalkie_flutter_ptt_up_with_roger',
  );

  late final int Function(int) _punchNat =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int64), int Function(int)>(
    'owalkie_flutter_punch_nat',
  );

  late final int Function(int) _recoverUdp =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int64), int Function(int)>(
    'owalkie_flutter_recover_udp',
  );

  late final void Function(int) _bindProcessNetwork =
      _lib.lookupFunction<ffi.Void Function(ffi.Int64), void Function(int)>(
    'owalkie_flutter_bind_process_network',
  );

  late final int Function(int, int) _reportSignal =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int32, ffi.Int32), int Function(int, int)>(
    'owalkie_flutter_report_signal',
  );

  late final int Function(int) _clearSignal =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int32), int Function(int)>(
    'owalkie_flutter_clear_signal',
  );

  late final int Function() _getUplinkSignalByte =
      _lib.lookupFunction<ffi.Int32 Function(), int Function()>(
    'owalkie_flutter_get_uplink_signal_byte',
  );

  static const int signalWifi = 0;
  static const int signalCell = 1;

  late final int Function(int, ffi.Pointer<ffi.Int16>, int, ffi.Pointer<ffi.Int16>, int, int)
      _sendCall = _lib.lookupFunction<
          ffi.Int32 Function(
            ffi.Int64,
            ffi.Pointer<ffi.Int16>,
            ffi.Uint64,
            ffi.Pointer<ffi.Int16>,
            ffi.Uint64,
            ffi.Int32,
          ),
          int Function(int, ffi.Pointer<ffi.Int16>, int, ffi.Pointer<ffi.Int16>, int, int)>(
    'owalkie_flutter_send_call',
  );

  bool get hasSession => _hasSession() != 0;

  int get codecSampleRate => _codecSampleRate();

  int get codecFrameSamples => _codecFrameSamples();

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

  int setRepeaterMode(int sessionId, {required bool enabled}) =>
      _setRepeater(sessionId, enabled ? 1 : 0);

  int punchNat(int sessionId) => _punchNat(sessionId);

  int recoverUdp(int sessionId) => _recoverUdp(sessionId);

  void bindProcessNetwork(int networkHandle) => _bindProcessNetwork(networkHandle);

  int reportSignal({required int mode, required int value}) =>
      _reportSignal(mode, value);

  int clearSignal(int mode) => _clearSignal(mode);

  int getUplinkSignalByte() => _getUplinkSignalByte();

  ({int resultCode, bool active}) checkChannelActivity({
    required String host,
    required int port,
    required String channel,
    int timeoutMs = 4000,
  }) {
    return using((arena) {
      final hostPtr = host.toNativeUtf8(allocator: arena);
      final channelPtr = channel.toNativeUtf8(allocator: arena);
      final outActive = arena<ffi.Int32>();
      final rc = _checkActivity(
        hostPtr.cast(),
        port,
        channelPtr.cast(),
        timeoutMs,
        outActive,
      );
      return (resultCode: rc, active: outActive.value != 0);
    });
  }

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

  Int16List? generateSignalPcm({
    required List<SignalPoint> points,
    required int sampleRate,
    int tailMs = 0,
    int repeatCount = 1,
    double gain = 0.26,
  }) {
    if (points.isEmpty) {
      return null;
    }
    return using((arena) {
      final n = points.length;
      final freqs = arena<ffi.Double>(n);
      final durs = arena<ffi.Int32>(n);
      for (var i = 0; i < n; i++) {
        freqs[i] = points[i].freqHz;
        durs[i] = points[i].durationMs;
      }
      final spec = arena<OwalkieFlutterSignalSpec>();
      spec.ref
        ..freqHz = freqs
        ..durationMs = durs
        ..pointCount = n
        ..tailMs = tailMs
        ..repeatCount = repeatCount
        ..gain = gain;
      final outSamples = arena<ffi.Pointer<ffi.Int16>>();
      final outCount = arena<ffi.Uint64>();
      final rc = _SignalGenerateNative(spec, sampleRate, outSamples, outCount);
      if (rc != 0) {
        return null;
      }
      final count = outCount.value;
      final copy = Int16List(count);
      for (var i = 0; i < count; i++) {
        copy[i] = outSamples.value[i];
      }
      _signalFree(outSamples.value);
      return copy;
    });
  }

  void playLocalPcm(Int16List pcm, {required int sampleRate}) {
    if (pcm.isEmpty) {
      return;
    }
    using((arena) {
      final buf = arena<ffi.Int16>(pcm.length);
      for (var i = 0; i < pcm.length; i++) {
        buf[i] = pcm[i];
      }
      _playLocal(buf, pcm.length, sampleRate);
    });
  }

  void startLocalPcmLoop(Int16List pcm, {required int sampleRate}) {
    if (pcm.isEmpty) {
      return;
    }
    using((arena) {
      final buf = arena<ffi.Int16>(pcm.length);
      for (var i = 0; i < pcm.length; i++) {
        buf[i] = pcm[i];
      }
      _startLocalLoop(buf, pcm.length, sampleRate);
    });
  }

  void stopLocalPcmLoop() {
    _stopLocalLoop();
  }

  int pttUpWithRoger({
    required int sessionId,
    Int16List? rogerUplink,
    Int16List? rogerLocal,
    required int localSampleRate,
  }) {
    return using((arena) {
      ffi.Pointer<ffi.Int16> upPtr = ffi.nullptr;
      ffi.Pointer<ffi.Int16> locPtr = ffi.nullptr;
      var upCount = 0;
      var locCount = 0;
      if (rogerUplink != null && rogerUplink.isNotEmpty) {
        upPtr = arena<ffi.Int16>(rogerUplink.length);
        for (var i = 0; i < rogerUplink.length; i++) {
          upPtr[i] = rogerUplink[i];
        }
        upCount = rogerUplink.length;
      }
      if (rogerLocal != null && rogerLocal.isNotEmpty) {
        locPtr = arena<ffi.Int16>(rogerLocal.length);
        for (var i = 0; i < rogerLocal.length; i++) {
          locPtr[i] = rogerLocal[i];
        }
        locCount = rogerLocal.length;
      }
      return _pttUpWithRoger(
        sessionId,
        upPtr,
        upCount,
        locPtr,
        locCount,
        localSampleRate,
      );
    });
  }

  int sendCallSignal({
    required int sessionId,
    required Int16List uplink,
    Int16List? local,
    required int localSampleRate,
  }) {
    return using((arena) {
      final upPtr = arena<ffi.Int16>(uplink.length);
      for (var i = 0; i < uplink.length; i++) {
        upPtr[i] = uplink[i];
      }
      ffi.Pointer<ffi.Int16> locPtr = ffi.nullptr;
      var locCount = 0;
      if (local != null && local.isNotEmpty) {
        locPtr = arena<ffi.Int16>(local.length);
        for (var i = 0; i < local.length; i++) {
          locPtr[i] = local[i];
        }
        locCount = local.length;
      }
      return _sendCall(sessionId, upPtr, uplink.length, locPtr, locCount, localSampleRate);
    });
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
