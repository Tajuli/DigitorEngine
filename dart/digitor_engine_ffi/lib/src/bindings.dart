import 'dart:ffi';

final class DigitorTimelineAudioSession extends Opaque {}

final class DigitorTimelineSessionConfigNative extends Struct {
  @Uint32()
  external int sampleRate;
  @Uint32()
  external int channels;
  @Int64()
  external int durationUs;
}

final class DigitorTimelinePublicationNative extends Struct {
  @Uint64()
  external int revision;
  @Int64()
  external int durationUs;
  @Uint32()
  external int videoTrackCount;
  @Uint32()
  external int audioTrackCount;
}

final class DigitorAudioSessionControlsNative extends Struct {
  @Double()
  external double masterGainDb;
  @Double()
  external double playbackRate;
  @Uint8()
  external int preservePitch;
  @Uint8()
  external int enableDynamics;
}

final class DigitorTimelineSessionStatusNative extends Struct {
  @Uint64()
  external int revision;
  @Uint64()
  external int seekEpoch;
  @Int64()
  external int positionUs;
  @Int64()
  external int durationUs;
  @Int32()
  external int playbackState;
  @Uint32()
  external int sampleRate;
  @Uint32()
  external int channels;
  @Double()
  external double masterGainDb;
  @Double()
  external double playbackRate;
  @Uint8()
  external int preservePitch;
  @Uint8()
  external int enableDynamics;
}

final class DigitorTimelineSessionTelemetryNative extends Struct {
  @Uint64()
  external int publications;
  @Uint64()
  external int rejectedPublications;
  @Uint64()
  external int playCommands;
  @Uint64()
  external int pauseCommands;
  @Uint64()
  external int stopCommands;
  @Uint64()
  external int seekCommands;
  @Uint64()
  external int controlUpdates;
}

typedef _CreateNative = Int32 Function(
  Pointer<DigitorTimelineSessionConfigNative>,
  Pointer<Pointer<DigitorTimelineAudioSession>>,
);
typedef _CreateDart = int Function(
  Pointer<DigitorTimelineSessionConfigNative>,
  Pointer<Pointer<DigitorTimelineAudioSession>>,
);
typedef _DestroyNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
);
typedef _DestroyDart = int Function(Pointer<DigitorTimelineAudioSession>);
typedef _PublishNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorTimelinePublicationNative>,
);
typedef _PublishDart = int Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorTimelinePublicationNative>,
);
typedef _CommandNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
);
typedef _CommandDart = int Function(Pointer<DigitorTimelineAudioSession>);
typedef _SeekNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
  Int64,
);
typedef _SeekDart = int Function(
  Pointer<DigitorTimelineAudioSession>,
  int,
);
typedef _ControlsNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorAudioSessionControlsNative>,
);
typedef _ControlsDart = int Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorAudioSessionControlsNative>,
);
typedef _StatusNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorTimelineSessionStatusNative>,
);
typedef _StatusDart = int Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorTimelineSessionStatusNative>,
);
typedef _TelemetryNative = Int32 Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorTimelineSessionTelemetryNative>,
);
typedef _TelemetryDart = int Function(
  Pointer<DigitorTimelineAudioSession>,
  Pointer<DigitorTimelineSessionTelemetryNative>,
);

final class DigitorTimelineBindings {
  DigitorTimelineBindings(DynamicLibrary library)
      : create = library.lookupFunction<_CreateNative, _CreateDart>(
          'digitor_timeline_session_create',
        ),
        destroy = library.lookupFunction<_DestroyNative, _DestroyDart>(
          'digitor_timeline_session_destroy',
        ),
        publish = library.lookupFunction<_PublishNative, _PublishDart>(
          'digitor_timeline_session_publish',
        ),
        play = library.lookupFunction<_CommandNative, _CommandDart>(
          'digitor_timeline_session_play',
        ),
        pause = library.lookupFunction<_CommandNative, _CommandDart>(
          'digitor_timeline_session_pause',
        ),
        stop = library.lookupFunction<_CommandNative, _CommandDart>(
          'digitor_timeline_session_stop',
        ),
        seek = library.lookupFunction<_SeekNative, _SeekDart>(
          'digitor_timeline_session_seek',
        ),
        setAudioControls = library.lookupFunction<
          _ControlsNative,
          _ControlsDart
        >('digitor_timeline_session_set_audio_controls'),
        getStatus = library.lookupFunction<_StatusNative, _StatusDart>(
          'digitor_timeline_session_get_status',
        ),
        getTelemetry = library.lookupFunction<
          _TelemetryNative,
          _TelemetryDart
        >('digitor_timeline_session_get_telemetry');

  final _CreateDart create;
  final _DestroyDart destroy;
  final _PublishDart publish;
  final _CommandDart play;
  final _CommandDart pause;
  final _CommandDart stop;
  final _SeekDart seek;
  final _ControlsDart setAudioControls;
  final _StatusDart getStatus;
  final _TelemetryDart getTelemetry;
}
