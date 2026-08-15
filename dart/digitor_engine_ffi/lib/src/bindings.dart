import 'dart:ffi';

import 'package:ffi/ffi.dart';

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

typedef DigitorCreateNative =
    Int32 Function(
      Pointer<DigitorTimelineSessionConfigNative>,
      Pointer<Pointer<DigitorTimelineAudioSession>>,
    );
typedef DigitorCreateDart =
    int Function(
      Pointer<DigitorTimelineSessionConfigNative>,
      Pointer<Pointer<DigitorTimelineAudioSession>>,
    );
typedef DigitorDestroyNative =
    Int32 Function(Pointer<DigitorTimelineAudioSession>);
typedef DigitorDestroyDart = int Function(Pointer<DigitorTimelineAudioSession>);
typedef DigitorPublishNative =
    Int32 Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorTimelinePublicationNative>,
    );
typedef DigitorPublishDart =
    int Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorTimelinePublicationNative>,
    );
typedef DigitorAttachMediaNative =
    Int32 Function(Pointer<DigitorTimelineAudioSession>, Pointer<Utf8>);
typedef DigitorAttachMediaDart =
    int Function(Pointer<DigitorTimelineAudioSession>, Pointer<Utf8>);
typedef DigitorCommandNative =
    Int32 Function(Pointer<DigitorTimelineAudioSession>);
typedef DigitorCommandDart = int Function(Pointer<DigitorTimelineAudioSession>);
typedef DigitorSeekNative =
    Int32 Function(Pointer<DigitorTimelineAudioSession>, Int64);
typedef DigitorSeekDart =
    int Function(Pointer<DigitorTimelineAudioSession>, int);
typedef DigitorControlsNative =
    Int32 Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorAudioSessionControlsNative>,
    );
typedef DigitorControlsDart =
    int Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorAudioSessionControlsNative>,
    );
typedef DigitorStatusNative =
    Int32 Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorTimelineSessionStatusNative>,
    );
typedef DigitorStatusDart =
    int Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorTimelineSessionStatusNative>,
    );
typedef DigitorTelemetryNative =
    Int32 Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorTimelineSessionTelemetryNative>,
    );
typedef DigitorTelemetryDart =
    int Function(
      Pointer<DigitorTimelineAudioSession>,
      Pointer<DigitorTimelineSessionTelemetryNative>,
    );

final class DigitorTimelineBindings {
  DigitorTimelineBindings(DynamicLibrary library)
    : create = library.lookupFunction<DigitorCreateNative, DigitorCreateDart>(
        'digitor_timeline_session_create',
      ),
      destroy = library
          .lookupFunction<DigitorDestroyNative, DigitorDestroyDart>(
            'digitor_timeline_session_destroy',
          ),
      publish = library
          .lookupFunction<DigitorPublishNative, DigitorPublishDart>(
            'digitor_timeline_session_publish',
          ),
      attachMedia = library
          .lookupFunction<DigitorAttachMediaNative, DigitorAttachMediaDart>(
            'digitor_timeline_session_attach_media',
          ),
      detachMedia = library
          .lookupFunction<DigitorCommandNative, DigitorCommandDart>(
            'digitor_timeline_session_detach_media',
          ),
      play = library.lookupFunction<DigitorCommandNative, DigitorCommandDart>(
        'digitor_timeline_session_play',
      ),
      pause = library.lookupFunction<DigitorCommandNative, DigitorCommandDart>(
        'digitor_timeline_session_pause',
      ),
      stop = library.lookupFunction<DigitorCommandNative, DigitorCommandDart>(
        'digitor_timeline_session_stop',
      ),
      seek = library.lookupFunction<DigitorSeekNative, DigitorSeekDart>(
        'digitor_timeline_session_seek',
      ),
      setAudioControls = library
          .lookupFunction<DigitorControlsNative, DigitorControlsDart>(
            'digitor_timeline_session_set_audio_controls',
          ),
      getStatus = library
          .lookupFunction<DigitorStatusNative, DigitorStatusDart>(
            'digitor_timeline_session_get_status',
          ),
      getTelemetry = library
          .lookupFunction<DigitorTelemetryNative, DigitorTelemetryDart>(
            'digitor_timeline_session_get_telemetry',
          );

  final DigitorCreateDart create;
  final DigitorDestroyDart destroy;
  final DigitorPublishDart publish;
  final DigitorAttachMediaDart attachMedia;
  final DigitorCommandDart detachMedia;
  final DigitorCommandDart play;
  final DigitorCommandDart pause;
  final DigitorCommandDart stop;
  final DigitorSeekDart seek;
  final DigitorControlsDart setAudioControls;
  final DigitorStatusDart getStatus;
  final DigitorTelemetryDart getTelemetry;
}
