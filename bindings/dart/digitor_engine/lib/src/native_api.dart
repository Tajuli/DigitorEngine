import 'dart:ffi';
import 'dart:io';

final class _NativeSession extends Opaque {}

final class _NativeConfig extends Struct {
  @Uint32()
  external int sampleRate;
  @Uint32()
  external int channels;
  @Int64()
  external int durationFrames;
}

final class _NativeAudioControls extends Struct {
  @Double()
  external double masterGainDb;
  @Double()
  external double playbackRate;
  @Uint8()
  external int preservePitch;
  @Uint8()
  external int enableDynamics;
}

final class _NativeStatus extends Struct {
  @Uint64()
  external int revision;
  @Int64()
  external int positionFrames;
  @Uint64()
  external int seekEpoch;
  @Int32()
  external int playbackState;
}

final class _NativeTelemetry extends Struct {
  @Uint64()
  external int publishedRevisions;
  @Uint64()
  external int playCommands;
  @Uint64()
  external int pauseCommands;
  @Uint64()
  external int stopCommands;
  @Uint64()
  external int seekCommands;
}

enum DigitorPlaybackState { stopped, playing, paused }

final class DigitorSessionConfig {
  const DigitorSessionConfig({
    this.sampleRate = 48000,
    this.channels = 2,
    required this.durationFrames,
  });
  final int sampleRate;
  final int channels;
  final int durationFrames;
}

final class DigitorAudioControls {
  const DigitorAudioControls({
    this.masterGainDb = 0,
    this.playbackRate = 1,
    this.preservePitch = true,
    this.enableDynamics = true,
  });
  final double masterGainDb;
  final double playbackRate;
  final bool preservePitch;
  final bool enableDynamics;
}

final class DigitorSessionStatus {
  const DigitorSessionStatus({
    required this.revision,
    required this.positionFrames,
    required this.seekEpoch,
    required this.playbackState,
  });
  final int revision;
  final int positionFrames;
  final int seekEpoch;
  final DigitorPlaybackState playbackState;
}

final class DigitorSessionTelemetry {
  const DigitorSessionTelemetry({
    required this.publishedRevisions,
    required this.playCommands,
    required this.pauseCommands,
    required this.stopCommands,
    required this.seekCommands,
  });
  final int publishedRevisions;
  final int playCommands;
  final int pauseCommands;
  final int stopCommands;
  final int seekCommands;
}

abstract interface class DigitorNativeApi {
  int create(DigitorSessionConfig config, Pointer<Pointer<_NativeSession>> out);
  int destroy(Pointer<_NativeSession> session);
  int publish(Pointer<_NativeSession> session, int revision);
  int play(Pointer<_NativeSession> session);
  int pause(Pointer<_NativeSession> session);
  int stop(Pointer<_NativeSession> session);
  int seek(Pointer<_NativeSession> session, int frame);
  int setAudioControls(Pointer<_NativeSession> session, DigitorAudioControls controls);
  int getStatus(Pointer<_NativeSession> session, Pointer<_NativeStatus> out);
  int getTelemetry(Pointer<_NativeSession> session, Pointer<_NativeTelemetry> out);
}

final class FfiDigitorNativeApi implements DigitorNativeApi {
  FfiDigitorNativeApi(DynamicLibrary library)
      : _create = library.lookupFunction<_CreateNative, _CreateDart>('digitor_timeline_session_create'),
        _destroy = library.lookupFunction<_HandleNative, _HandleDart>('digitor_timeline_session_destroy'),
        _publish = library.lookupFunction<_PublishNative, _PublishDart>('digitor_timeline_session_publish'),
        _play = library.lookupFunction<_HandleNative, _HandleDart>('digitor_timeline_session_play'),
        _pause = library.lookupFunction<_HandleNative, _HandleDart>('digitor_timeline_session_pause'),
        _stop = library.lookupFunction<_HandleNative, _HandleDart>('digitor_timeline_session_stop'),
        _seek = library.lookupFunction<_SeekNative, _SeekDart>('digitor_timeline_session_seek'),
        _setAudio = library.lookupFunction<_SetAudioNative, _SetAudioDart>('digitor_timeline_session_set_audio_controls'),
        _getStatus = library.lookupFunction<_GetStatusNative, _GetStatusDart>('digitor_timeline_session_get_status'),
        _getTelemetry = library.lookupFunction<_GetTelemetryNative, _GetTelemetryDart>('digitor_timeline_session_get_telemetry');

  factory FfiDigitorNativeApi.open({String? path}) =>
      FfiDigitorNativeApi(path == null ? _openDefaultLibrary() : DynamicLibrary.open(path));

  final _CreateDart _create;
  final _HandleDart _destroy;
  final _PublishDart _publish;
  final _HandleDart _play;
  final _HandleDart _pause;
  final _HandleDart _stop;
  final _SeekDart _seek;
  final _SetAudioDart _setAudio;
  final _GetStatusDart _getStatus;
  final _GetTelemetryDart _getTelemetry;

  @override
  int create(DigitorSessionConfig config, Pointer<Pointer<_NativeSession>> out) {
    final native = calloc<_NativeConfig>();
    try {
      native.ref
        ..sampleRate = config.sampleRate
        ..channels = config.channels
        ..durationFrames = config.durationFrames;
      return _create(native, out);
    } finally {
      calloc.free(native);
    }
  }

  @override int destroy(Pointer<_NativeSession> session) => _destroy(session);
  @override int publish(Pointer<_NativeSession> session, int revision) => _publish(session, revision);
  @override int play(Pointer<_NativeSession> session) => _play(session);
  @override int pause(Pointer<_NativeSession> session) => _pause(session);
  @override int stop(Pointer<_NativeSession> session) => _stop(session);
  @override int seek(Pointer<_NativeSession> session, int frame) => _seek(session, frame);

  @override
  int setAudioControls(Pointer<_NativeSession> session, DigitorAudioControls controls) {
    final native = calloc<_NativeAudioControls>();
    try {
      native.ref
        ..masterGainDb = controls.masterGainDb
        ..playbackRate = controls.playbackRate
        ..preservePitch = controls.preservePitch ? 1 : 0
        ..enableDynamics = controls.enableDynamics ? 1 : 0;
      return _setAudio(session, native);
    } finally {
      calloc.free(native);
    }
  }

  @override int getStatus(Pointer<_NativeSession> session, Pointer<_NativeStatus> out) => _getStatus(session, out);
  @override int getTelemetry(Pointer<_NativeSession> session, Pointer<_NativeTelemetry> out) => _getTelemetry(session, out);
}

DynamicLibrary _openDefaultLibrary() {
  if (Platform.isWindows) return DynamicLibrary.open('digitor_engine.dll');
  if (Platform.isAndroid || Platform.isLinux) return DynamicLibrary.open('libdigitor_engine.so');
  if (Platform.isMacOS || Platform.isIOS) return DynamicLibrary.process();
  throw UnsupportedError('Unsupported platform for DigitorEngine');
}

typedef _CreateNative = Int32 Function(Pointer<_NativeConfig>, Pointer<Pointer<_NativeSession>>);
typedef _CreateDart = int Function(Pointer<_NativeConfig>, Pointer<Pointer<_NativeSession>>);
typedef _HandleNative = Int32 Function(Pointer<_NativeSession>);
typedef _HandleDart = int Function(Pointer<_NativeSession>);
typedef _PublishNative = Int32 Function(Pointer<_NativeSession>, Uint64);
typedef _PublishDart = int Function(Pointer<_NativeSession>, int);
typedef _SeekNative = Int32 Function(Pointer<_NativeSession>, Int64);
typedef _SeekDart = int Function(Pointer<_NativeSession>, int);
typedef _SetAudioNative = Int32 Function(Pointer<_NativeSession>, Pointer<_NativeAudioControls>);
typedef _SetAudioDart = int Function(Pointer<_NativeSession>, Pointer<_NativeAudioControls>);
typedef _GetStatusNative = Int32 Function(Pointer<_NativeSession>, Pointer<_NativeStatus>);
typedef _GetStatusDart = int Function(Pointer<_NativeSession>, Pointer<_NativeStatus>);
typedef _GetTelemetryNative = Int32 Function(Pointer<_NativeSession>, Pointer<_NativeTelemetry>);
typedef _GetTelemetryDart = int Function(Pointer<_NativeSession>, Pointer<_NativeTelemetry>);
