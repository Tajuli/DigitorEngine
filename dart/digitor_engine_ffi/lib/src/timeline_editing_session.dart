import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'library_loader.dart';

final class DigitorTimelineCompletionHandleNative extends Opaque {}

final class DigitorTimelineProjectInfoNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint64()
  external int revision;

  @Int64()
  external int durationUs;

  @Uint32()
  external int videoTrackCount;

  @Uint32()
  external int audioTrackCount;

  @Uint32()
  external int clipCount;

  @Int32()
  external int valid;
}

enum DigitorTimelineTrackKind { video, audio }
enum DigitorTimelineClipKind { video, image, overlay, text, audio }

final class DigitorTimelineProjectInfo {
  const DigitorTimelineProjectInfo({
    required this.revision,
    required this.durationUs,
    required this.videoTrackCount,
    required this.audioTrackCount,
    required this.clipCount,
    required this.valid,
  });

  final int revision;
  final int durationUs;
  final int videoTrackCount;
  final int audioTrackCount;
  final int clipCount;
  final bool valid;
}

typedef _CreateNative = Pointer<DigitorTimelineCompletionHandleNative> Function();
typedef _CreateDart = Pointer<DigitorTimelineCompletionHandleNative> Function();
typedef _DestroyNative = Void Function(Pointer<DigitorTimelineCompletionHandleNative>);
typedef _DestroyDart = void Function(Pointer<DigitorTimelineCompletionHandleNative>);
typedef _AddTrackNative = Int32 Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  Pointer<Utf8>,
  Int32,
);
typedef _AddTrackDart = int Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  Pointer<Utf8>,
  int,
);
typedef _AddClipNative = Int32 Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  Pointer<Utf8>,
  Int32,
  Int64,
  Int64,
  Int64,
  Int64,
  Pointer<Utf8>,
  Pointer<Utf8>,
  Int32,
);
typedef _AddClipDart = int Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  Pointer<Utf8>,
  int,
  int,
  int,
  int,
  int,
  Pointer<Utf8>,
  Pointer<Utf8>,
  int,
);
typedef _SplitClipNative = Int32 Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  Int64,
  Pointer<Utf8>,
  Int32,
);
typedef _SplitClipDart = int Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  int,
  Pointer<Utf8>,
  int,
);
typedef _RemoveClipNative = Int32 Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  Int32,
);
typedef _RemoveClipDart = int Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<Utf8>,
  int,
);
typedef _ProjectInfoNative = Int32 Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<DigitorTimelineProjectInfoNative>,
);
typedef _ProjectInfoDart = int Function(
  Pointer<DigitorTimelineCompletionHandleNative>,
  Pointer<DigitorTimelineProjectInfoNative>,
);

/// Native, revisioned multi-clip timeline edit session.
///
/// This object owns only timeline edit state. Decode, GPU render, node/effect
/// processing, preview presentation and export remain in DigitorEngine's
/// production pipeline; Flutter never performs per-pixel processing here.
final class DigitorTimelineEditingSession {
  DigitorTimelineEditingSession._(
    DynamicLibrary library,
    this._handle,
  ) : _destroy = library.lookupFunction<_DestroyNative, _DestroyDart>(
          'digitor_timeline_completion_destroy',
        ),
        _addTrack = library.lookupFunction<_AddTrackNative, _AddTrackDart>(
          'digitor_timeline_completion_add_track',
        ),
        _addClip = library.lookupFunction<_AddClipNative, _AddClipDart>(
          'digitor_timeline_completion_add_clip',
        ),
        _splitClip = library.lookupFunction<_SplitClipNative, _SplitClipDart>(
          'digitor_timeline_completion_split_clip',
        ),
        _removeClip = library.lookupFunction<_RemoveClipNative, _RemoveClipDart>(
          'digitor_timeline_completion_remove_clip',
        ),
        _projectInfo = library.lookupFunction<_ProjectInfoNative, _ProjectInfoDart>(
          'digitor_timeline_completion_project_info',
        );

  factory DigitorTimelineEditingSession.create({String? libraryPath}) {
    final library = DigitorLibraryLoader.open(overridePath: libraryPath);
    final create = library.lookupFunction<_CreateNative, _CreateDart>(
      'digitor_timeline_completion_create',
    );
    final handle = create();
    if (handle == nullptr) {
      throw StateError('DigitorEngine failed to create timeline editing session.');
    }
    return DigitorTimelineEditingSession._(library, handle);
  }

  Pointer<DigitorTimelineCompletionHandleNative> _handle;
  final _DestroyDart _destroy;
  final _AddTrackDart _addTrack;
  final _AddClipDart _addClip;
  final _SplitClipDart _splitClip;
  final _RemoveClipDart _removeClip;
  final _ProjectInfoDart _projectInfo;

  bool get disposed => _handle == nullptr;

  void addTrack({
    required String id,
    required String name,
    required DigitorTimelineTrackKind kind,
  }) {
    _ensureAlive();
    final nativeId = id.toNativeUtf8();
    final nativeName = name.toNativeUtf8();
    try {
      _check('addTrack', _addTrack(_handle, nativeId, nativeName, kind.index));
    } finally {
      calloc.free(nativeName);
      calloc.free(nativeId);
    }
  }

  void addClip({
    required String trackId,
    required String clipId,
    required DigitorTimelineClipKind kind,
    required int startUs,
    required int durationUs,
    int sourceStartUs = 0,
    int sourceDurationUs = 0,
    String sourceMediaGroupId = '',
    String linkGroupId = '',
    bool embeddedAudio = false,
  }) {
    _ensureAlive();
    final nativeTrack = trackId.toNativeUtf8();
    final nativeClip = clipId.toNativeUtf8();
    final nativeSource = sourceMediaGroupId.toNativeUtf8();
    final nativeLink = linkGroupId.toNativeUtf8();
    try {
      _check(
        'addClip',
        _addClip(
          _handle,
          nativeTrack,
          nativeClip,
          kind.index,
          startUs,
          durationUs,
          sourceStartUs,
          sourceDurationUs,
          nativeSource,
          nativeLink,
          embeddedAudio ? 1 : 0,
        ),
      );
    } finally {
      calloc.free(nativeLink);
      calloc.free(nativeSource);
      calloc.free(nativeClip);
      calloc.free(nativeTrack);
    }
  }

  void splitClip({
    required String clipId,
    required int positionUs,
    required String secondClipId,
    bool splitLinked = true,
  }) {
    _ensureAlive();
    final nativeClip = clipId.toNativeUtf8();
    final nativeSecond = secondClipId.toNativeUtf8();
    try {
      _check(
        'splitClip',
        _splitClip(
          _handle,
          nativeClip,
          positionUs,
          nativeSecond,
          splitLinked ? 1 : 0,
        ),
      );
    } finally {
      calloc.free(nativeSecond);
      calloc.free(nativeClip);
    }
  }

  void removeClip(String clipId, {bool removeLinked = true}) {
    _ensureAlive();
    final nativeClip = clipId.toNativeUtf8();
    try {
      _check(
        'removeClip',
        _removeClip(_handle, nativeClip, removeLinked ? 1 : 0),
      );
    } finally {
      calloc.free(nativeClip);
    }
  }

  DigitorTimelineProjectInfo get info {
    _ensureAlive();
    final native = calloc<DigitorTimelineProjectInfoNative>();
    try {
      native.ref.structSize = sizeOf<DigitorTimelineProjectInfoNative>();
      _check('projectInfo', _projectInfo(_handle, native));
      return DigitorTimelineProjectInfo(
        revision: native.ref.revision,
        durationUs: native.ref.durationUs,
        videoTrackCount: native.ref.videoTrackCount,
        audioTrackCount: native.ref.audioTrackCount,
        clipCount: native.ref.clipCount,
        valid: native.ref.valid != 0,
      );
    } finally {
      calloc.free(native);
    }
  }

  void dispose() {
    if (_handle == nullptr) return;
    _destroy(_handle);
    _handle = nullptr;
  }

  void _ensureAlive() {
    if (_handle == nullptr) {
      throw StateError('Timeline editing session is disposed.');
    }
  }

  static void _check(String operation, int result) {
    if (result == 0) {
      throw StateError('DigitorEngine timeline $operation failed validation.');
    }
  }
}
