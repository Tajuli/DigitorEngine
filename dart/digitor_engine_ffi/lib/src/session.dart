import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'library_loader.dart';

enum DigitorPlaybackState { stopped, paused, playing }

final class DigitorNativeException implements Exception {
  const DigitorNativeException(this.operation, this.resultCode);

  final String operation;
  final int resultCode;

  @override
  String toString() => 'DigitorNativeException($operation, result=$resultCode)';
}

final class DigitorTimelineStatus {
  const DigitorTimelineStatus({
    required this.revision,
    required this.seekEpoch,
    required this.positionUs,
    required this.durationUs,
    required this.playbackState,
    required this.sampleRate,
    required this.channels,
    required this.masterGainDb,
    required this.playbackRate,
    required this.preservePitch,
    required this.enableDynamics,
  });

  final int revision;
  final int seekEpoch;
  final int positionUs;
  final int durationUs;
  final DigitorPlaybackState playbackState;
  final int sampleRate;
  final int channels;
  final double masterGainDb;
  final double playbackRate;
  final bool preservePitch;
  final bool enableDynamics;
}

final class DigitorTimelineTelemetry {
  const DigitorTimelineTelemetry({
    required this.publications,
    required this.rejectedPublications,
    required this.playCommands,
    required this.pauseCommands,
    required this.stopCommands,
    required this.seekCommands,
    required this.controlUpdates,
  });

  final int publications;
  final int rejectedPublications;
  final int playCommands;
  final int pauseCommands;
  final int stopCommands;
  final int seekCommands;
  final int controlUpdates;
}

final class DigitorTimelineSession {
  DigitorTimelineSession._(this._bindings, this._handle);

  factory DigitorTimelineSession.create({
    required int sampleRate,
    required int channels,
    required int durationUs,
    String? libraryPath,
  }) {
    if (sampleRate <= 0 || channels <= 0 || durationUs < 0) {
      throw ArgumentError('Invalid timeline session configuration.');
    }
    final bindings = DigitorTimelineBindings(
      DigitorLibraryLoader.open(overridePath: libraryPath),
    );
    final config = calloc<DigitorTimelineSessionConfigNative>();
    final out = calloc<Pointer<DigitorTimelineAudioSession>>();
    try {
      config.ref
        ..sampleRate = sampleRate
        ..channels = channels
        ..durationUs = durationUs;
      _check('create', bindings.create(config, out));
      if (out.value == nullptr) {
        throw const DigitorNativeException('create', 100);
      }
      return DigitorTimelineSession._(bindings, out.value);
    } finally {
      calloc.free(config);
      calloc.free(out);
    }
  }

  final DigitorTimelineBindings _bindings;
  Pointer<DigitorTimelineAudioSession> _handle;
  bool _disposed = false;
  int _lastRevision = 0;

  void publish({
    required int revision,
    required int durationUs,
    required int videoTrackCount,
    required int audioTrackCount,
  }) {
    _ensureAlive();
    if (revision <= _lastRevision) {
      throw ArgumentError('Timeline revision must increase monotonically.');
    }
    final value = calloc<DigitorTimelinePublicationNative>();
    try {
      value.ref
        ..revision = revision
        ..durationUs = durationUs
        ..videoTrackCount = videoTrackCount
        ..audioTrackCount = audioTrackCount;
      _check('publish', _bindings.publish(_handle, value));
      _lastRevision = revision;
    } finally {
      calloc.free(value);
    }
  }

  void play() {
    _ensureAlive();
    _check('play', _bindings.play(_handle));
  }

  void pause() {
    _ensureAlive();
    _check('pause', _bindings.pause(_handle));
  }

  void stop() {
    _ensureAlive();
    _check('stop', _bindings.stop(_handle));
  }

  void seek(int positionUs) {
    _ensureAlive();
    _check('seek', _bindings.seek(_handle, positionUs));
  }

  void setAudioControls({
    required double masterGainDb,
    required double playbackRate,
    required bool preservePitch,
    required bool enableDynamics,
  }) {
    _ensureAlive();
    final value = calloc<DigitorAudioSessionControlsNative>();
    try {
      value.ref
        ..masterGainDb = masterGainDb
        ..playbackRate = playbackRate
        ..preservePitch = preservePitch ? 1 : 0
        ..enableDynamics = enableDynamics ? 1 : 0;
      _check('setAudioControls', _bindings.setAudioControls(_handle, value));
    } finally {
      calloc.free(value);
    }
  }

  DigitorTimelineStatus status() {
    _ensureAlive();
    final value = calloc<DigitorTimelineSessionStatusNative>();
    try {
      _check('getStatus', _bindings.getStatus(_handle, value));
      final s = value.ref;
      final state =
          s.playbackState >= 0 &&
              s.playbackState < DigitorPlaybackState.values.length
          ? DigitorPlaybackState.values[s.playbackState]
          : DigitorPlaybackState.stopped;
      return DigitorTimelineStatus(
        revision: s.revision,
        seekEpoch: s.seekEpoch,
        positionUs: s.positionUs,
        durationUs: s.durationUs,
        playbackState: state,
        sampleRate: s.sampleRate,
        channels: s.channels,
        masterGainDb: s.masterGainDb,
        playbackRate: s.playbackRate,
        preservePitch: s.preservePitch != 0,
        enableDynamics: s.enableDynamics != 0,
      );
    } finally {
      calloc.free(value);
    }
  }

  DigitorTimelineTelemetry telemetry() {
    _ensureAlive();
    final value = calloc<DigitorTimelineSessionTelemetryNative>();
    try {
      _check('getTelemetry', _bindings.getTelemetry(_handle, value));
      final t = value.ref;
      return DigitorTimelineTelemetry(
        publications: t.publications,
        rejectedPublications: t.rejectedPublications,
        playCommands: t.playCommands,
        pauseCommands: t.pauseCommands,
        stopCommands: t.stopCommands,
        seekCommands: t.seekCommands,
        controlUpdates: t.controlUpdates,
      );
    } finally {
      calloc.free(value);
    }
  }

  void dispose() {
    if (_disposed) return;
    _check('destroy', _bindings.destroy(_handle));
    _disposed = true;
    _handle = nullptr;
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) {
      throw StateError('DigitorTimelineSession is disposed.');
    }
  }

  static void _check(String operation, int result) {
    if (result != 0) {
      throw DigitorNativeException(operation, result);
    }
  }
}
