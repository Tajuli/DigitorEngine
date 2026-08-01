import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

final class DigitorNativeSession extends Opaque {}
final class _NativeConfig extends Struct { @Uint32() external int sampleRate; @Uint32() external int channels; @Int64() external int durationFrames; }
final class _NativeAudioControls extends Struct { @Double() external double masterGainDb; @Double() external double playbackRate; @Uint8() external int preservePitch; @Uint8() external int enableDynamics; }
final class _NativeStatus extends Struct { @Uint64() external int revision; @Int64() external int positionFrames; @Uint64() external int seekEpoch; @Int32() external int playbackState; }
final class _NativeTelemetry extends Struct { @Uint64() external int publishedRevisions; @Uint64() external int playCommands; @Uint64() external int pauseCommands; @Uint64() external int stopCommands; @Uint64() external int seekCommands; }

enum DigitorPlaybackState { stopped, playing, paused }

final class DigitorSessionConfig {
  const DigitorSessionConfig({this.sampleRate = 48000, this.channels = 2, required this.durationFrames});
  final int sampleRate; final int channels; final int durationFrames;
}
final class DigitorAudioControls {
  const DigitorAudioControls({this.masterGainDb = 0, this.playbackRate = 1, this.preservePitch = true, this.enableDynamics = true});
  final double masterGainDb; final double playbackRate; final bool preservePitch; final bool enableDynamics;
}
final class DigitorSessionStatus {
  const DigitorSessionStatus({required this.revision, required this.positionFrames, required this.seekEpoch, required this.playbackState});
  final int revision; final int positionFrames; final int seekEpoch; final DigitorPlaybackState playbackState;
}
final class DigitorSessionTelemetry {
  const DigitorSessionTelemetry({required this.publishedRevisions, required this.playCommands, required this.pauseCommands, required this.stopCommands, required this.seekCommands});
  final int publishedRevisions; final int playCommands; final int pauseCommands; final int stopCommands; final int seekCommands;
}
final class DigitorNativeCall<T> { const DigitorNativeCall(this.result, this.value); final int result; final T? value; }

abstract interface class DigitorNativeApi {
  DigitorNativeCall<Pointer<DigitorNativeSession>> create(DigitorSessionConfig config);
  int destroy(Pointer<DigitorNativeSession> session);
  int publish(Pointer<DigitorNativeSession> session, int revision);
  int play(Pointer<DigitorNativeSession> session);
  int pause(Pointer<DigitorNativeSession> session);
  int stop(Pointer<DigitorNativeSession> session);
  int seek(Pointer<DigitorNativeSession> session, int frame);
  int setAudioControls(Pointer<DigitorNativeSession> session, DigitorAudioControls controls);
  DigitorNativeCall<DigitorSessionStatus> getStatus(Pointer<DigitorNativeSession> session);
  DigitorNativeCall<DigitorSessionTelemetry> getTelemetry(Pointer<DigitorNativeSession> session);
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
  factory FfiDigitorNativeApi.open({String? path}) => FfiDigitorNativeApi(path == null ? _openDefaultLibrary() : DynamicLibrary.open(path));
  final _CreateDart _create; final _HandleDart _destroy; final _PublishDart _publish; final _HandleDart _play; final _HandleDart _pause; final _HandleDart _stop; final _SeekDart _seek; final _SetAudioDart _setAudio; final _GetStatusDart _getStatus; final _GetTelemetryDart _getTelemetry;

  @override DigitorNativeCall<Pointer<DigitorNativeSession>> create(DigitorSessionConfig config) {
    final native = calloc<_NativeConfig>(); final out = calloc<Pointer<DigitorNativeSession>>();
    try { native.ref..sampleRate=config.sampleRate..channels=config.channels..durationFrames=config.durationFrames; final result=_create(native,out); return DigitorNativeCall(result, result==0 ? out.value : null); } finally { calloc.free(native); calloc.free(out); }
  }
  @override int destroy(Pointer<DigitorNativeSession> session)=>_destroy(session);
  @override int publish(Pointer<DigitorNativeSession> session,int revision)=>_publish(session,revision);
  @override int play(Pointer<DigitorNativeSession> session)=>_play(session);
  @override int pause(Pointer<DigitorNativeSession> session)=>_pause(session);
  @override int stop(Pointer<DigitorNativeSession> session)=>_stop(session);
  @override int seek(Pointer<DigitorNativeSession> session,int frame)=>_seek(session,frame);
  @override int setAudioControls(Pointer<DigitorNativeSession> session,DigitorAudioControls controls){ final native=calloc<_NativeAudioControls>(); try{ native.ref..masterGainDb=controls.masterGainDb..playbackRate=controls.playbackRate..preservePitch=(controls.preservePitch?1:0)..enableDynamics=(controls.enableDynamics?1:0); return _setAudio(session,native);}finally{calloc.free(native);} }
  @override DigitorNativeCall<DigitorSessionStatus> getStatus(Pointer<DigitorNativeSession> session){ final out=calloc<_NativeStatus>(); try{ final result=_getStatus(session,out); if(result!=0)return DigitorNativeCall(result,null); final index=out.ref.playbackState.clamp(0,DigitorPlaybackState.values.length-1); return DigitorNativeCall(result,DigitorSessionStatus(revision:out.ref.revision,positionFrames:out.ref.positionFrames,seekEpoch:out.ref.seekEpoch,playbackState:DigitorPlaybackState.values[index])); }finally{calloc.free(out);} }
  @override DigitorNativeCall<DigitorSessionTelemetry> getTelemetry(Pointer<DigitorNativeSession> session){ final out=calloc<_NativeTelemetry>(); try{ final result=_getTelemetry(session,out); if(result!=0)return DigitorNativeCall(result,null); return DigitorNativeCall(result,DigitorSessionTelemetry(publishedRevisions:out.ref.publishedRevisions,playCommands:out.ref.playCommands,pauseCommands:out.ref.pauseCommands,stopCommands:out.ref.stopCommands,seekCommands:out.ref.seekCommands)); }finally{calloc.free(out);} }
}

DynamicLibrary _openDefaultLibrary(){ if(Platform.isWindows)return DynamicLibrary.open('digitor_engine.dll'); if(Platform.isAndroid||Platform.isLinux)return DynamicLibrary.open('libdigitor_engine.so'); if(Platform.isMacOS||Platform.isIOS)return DynamicLibrary.process(); throw UnsupportedError('Unsupported platform for DigitorEngine'); }
typedef _CreateNative=Int32 Function(Pointer<_NativeConfig>,Pointer<Pointer<DigitorNativeSession>>); typedef _CreateDart=int Function(Pointer<_NativeConfig>,Pointer<Pointer<DigitorNativeSession>>);
typedef _HandleNative=Int32 Function(Pointer<DigitorNativeSession>); typedef _HandleDart=int Function(Pointer<DigitorNativeSession>);
typedef _PublishNative=Int32 Function(Pointer<DigitorNativeSession>,Uint64); typedef _PublishDart=int Function(Pointer<DigitorNativeSession>,int);
typedef _SeekNative=Int32 Function(Pointer<DigitorNativeSession>,Int64); typedef _SeekDart=int Function(Pointer<DigitorNativeSession>,int);
typedef _SetAudioNative=Int32 Function(Pointer<DigitorNativeSession>,Pointer<_NativeAudioControls>); typedef _SetAudioDart=int Function(Pointer<DigitorNativeSession>,Pointer<_NativeAudioControls>);
typedef _GetStatusNative=Int32 Function(Pointer<DigitorNativeSession>,Pointer<_NativeStatus>); typedef _GetStatusDart=int Function(Pointer<DigitorNativeSession>,Pointer<_NativeStatus>);
typedef _GetTelemetryNative=Int32 Function(Pointer<DigitorNativeSession>,Pointer<_NativeTelemetry>); typedef _GetTelemetryDart=int Function(Pointer<DigitorNativeSession>,Pointer<_NativeTelemetry>);
