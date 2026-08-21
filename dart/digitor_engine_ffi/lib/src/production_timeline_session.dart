import 'dart:convert';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'engine.dart';
import 'native_api.dart';
import 'native_production_api.dart';
import 'native_timeline_production_api.dart';
import 'node_graph.dart';
import 'production.dart';
import 'registered_production_session.dart';

final class DigitorTimelineMediaSource {
  const DigitorTimelineMediaSource({
    required this.sourceMediaGroupId,
    required this.path,
  });

  final String sourceMediaGroupId;
  final String path;
}

/// Native project-timeline preview/export facade.
///
/// The session owns no decoder or pixel buffer in Dart. A serialized native
/// timeline plus source registry is handed to DigitorEngine; global timeline
/// timestamps are resolved to source-local timestamps inside the native GPU
/// production service. Preview and export execute the exact same node/color
/// graph and remain fail-closed rather than silently selecting one source.
final class DigitorProductionTimelineSession {
  DigitorProductionTimelineSession(this._graph);

  final DigitorNodeGraph _graph;
  int? _outstandingPreviewGeneration;
  DigitorDefaultExportFrameContract? _lastPreviewFrameContract;
  int? _lastPreviewGraphRevision;
  int? _lastPreviewParameterRevision;
  bool _closed = false;

  bool get configured =>
      !_closed && digitorFlutterProductionTimelineConfigured() != 0;

  void configure({
    required String serializedProject,
    required List<DigitorTimelineMediaSource> sources,
  }) {
    _ensureOpen();
    if (serializedProject.isEmpty || sources.isEmpty) {
      throw ArgumentError('A serialized timeline and source registry are required.');
    }
    previewConsumed();

    final project = serializedProject.toNativeUtf8();
    final nativeSources = calloc<DigitorFlutterTimelineSourceNative>(sources.length);
    final allocatedGroups = <Pointer<Utf8>>[];
    final allocatedPaths = <Pointer<Utf8>>[];
    try {
      for (var index = 0; index < sources.length; index++) {
        final source = sources[index];
        if (source.sourceMediaGroupId.isEmpty || source.path.isEmpty) {
          throw ArgumentError('Timeline source ids and paths must not be empty.');
        }
        final group = source.sourceMediaGroupId.toNativeUtf8();
        final path = source.path.toNativeUtf8();
        allocatedGroups.add(group);
        allocatedPaths.add(path);
        nativeSources[index]
          ..structSize = sizeOf<DigitorFlutterTimelineSourceNative>()
          ..apiVersion = 1
          ..sourceMediaGroupId = group
          ..path = path;
      }
      _check(
        'timelineConfigure',
        digitorFlutterProductionTimelineConfigure(
          project,
          utf8.encode(serializedProject).length,
          nativeSources,
          sources.length,
        ),
      );
      _lastPreviewFrameContract = null;
      _lastPreviewGraphRevision = null;
      _lastPreviewParameterRevision = null;
    } finally {
      for (final value in allocatedPaths) {
        calloc.free(value);
      }
      for (final value in allocatedGroups) {
        calloc.free(value);
      }
      calloc.free(nativeSources);
      calloc.free(project);
    }
  }

  void clear() {
    if (_closed) return;
    previewConsumed();
    if (digitorFlutterProductionTimelineConfigured() != 0) {
      _check('timelineClear', digitorFlutterProductionTimelineClear());
    }
    _lastPreviewFrameContract = null;
    _lastPreviewGraphRevision = null;
    _lastPreviewParameterRevision = null;
  }

  DigitorNativeGpuTextureFrame preview({
    required int timestampUs,
    required int width,
    required int height,
  }) {
    _ensureConfigured();
    if (_outstandingPreviewGeneration != null) {
      throw StateError('Consume the current timeline preview before requesting another.');
    }
    if (timestampUs < 0 || width <= 0 || height <= 0) {
      throw ArgumentError('Invalid timeline preview timestamp or dimensions.');
    }

    final rendererBackend = digitorCurrentRendererBackendForExport();
    final native = calloc<DigitorNativeGpuTextureDescriptorNative>();
    try {
      native.ref
        ..structSize = sizeOf<DigitorNativeGpuTextureDescriptorNative>()
        ..apiVersion = 1;
      _check(
        'timelinePreview',
        digitorFlutterProductionTimelinePreview(
          _graph.nativeHandle,
          _graph.graphRevision,
          _graph.parameterRevision,
          timestampUs,
          width,
          height,
          native,
        ),
      );
      final value = native.ref;
      final frame = DigitorNativeGpuTextureFrame(
        backend: DigitorNativeTextureBackend.fromNative(value.backend),
        handleType: DigitorNativeTextureHandleType.fromNative(value.handleType),
        nativeHandle: value.nativeHandle,
        secondaryHandle: value.secondaryHandle,
        width: value.width,
        height: value.height,
        pixelFormat: DigitorPixelFormat.fromNative(value.pixelFormat),
        alphaMode: value.alphaMode,
        colorPrimaries: value.colorPrimaries,
        transferFunction: value.transferFunction,
        matrixCoefficients: value.matrixCoefficients,
        colorRange: value.colorRange,
        timestampUs: value.timestampUs,
        generation: value.generation,
        deviceIdentity: value.deviceIdentity,
        contextIdentity: value.contextIdentity,
        acquireSyncHandle: value.acquireSyncHandle,
        acquireSyncValue: value.acquireSyncValue,
        releaseSyncHandle: value.releaseSyncHandle,
        releaseSyncValue: value.releaseSyncValue,
        ownershipToken: value.ownershipToken,
        protectedContent: value.protectedContent != 0,
        readiness: DigitorNativeTextureReadiness.fromNative(value.readiness),
      );
      _outstandingPreviewGeneration = frame.generation;
      _lastPreviewFrameContract = digitorPreviewExportFrameContract(
        rendererBackend: rendererBackend,
        presentationFormat: frame.pixelFormat,
      );
      _lastPreviewGraphRevision = _graph.graphRevision;
      _lastPreviewParameterRevision = _graph.parameterRevision;
      return frame;
    } finally {
      calloc.free(native);
    }
  }

  void previewConsumed([int? generation]) {
    if (_closed) return;
    final expected = _outstandingPreviewGeneration;
    if (expected == null) return;
    final resolved = generation ?? expected;
    if (resolved != expected) {
      throw ArgumentError.value(generation, 'generation');
    }
    _check(
      'timelinePreviewConsumed',
      digitorFlutterProductionTimelinePreviewConsumed(resolved),
    );
    _outstandingPreviewGeneration = null;
  }

  void export({
    required String path,
    required int firstFrame,
    required int lastFrame,
    required int width,
    required int height,
    required int snapshotIdentity,
    required int timelineRevision,
    required int renderRevision,
    required int nodeGraphRevision,
    required int colorPipelineRevision,
    required int audioRevision,
    required String graphRecipeIdentity,
    int? workingFormat,
    String? colorMetadata,
    int fpsNum = 30,
    int fpsDen = 1,
    int videoBitrate = 12000000,
    bool hdr = false,
    bool tenBit = false,
    DigitorExportFormat format = DigitorExportFormat.mp4,
    DigitorVideoCodec codec = DigitorVideoCodec.h264,
    void Function(DigitorExportProgress progress)? onProgress,
  }) {
    _ensureConfigured();

    final previewContractIsCurrent =
        _lastPreviewFrameContract != null &&
        _lastPreviewGraphRevision == nodeGraphRevision &&
        _lastPreviewParameterRevision == colorPipelineRevision;
    final hasStalePreviewContract =
        _lastPreviewFrameContract != null && !previewContractIsCurrent;
    if (workingFormat == null && hasStalePreviewContract) {
      throw StateError(
        'The production graph changed after the last preview. Render a fresh preview before freezing export.',
      );
    }
    final currentPreviewContract = previewContractIsCurrent
        ? _lastPreviewFrameContract
        : null;

    previewConsumed();
    if (firstFrame < 0 || lastFrame < firstFrame || width <= 0 || height <= 0) {
      throw ArgumentError('Invalid frozen timeline export frame range or dimensions.');
    }
    final defaultFrameContract = digitorDefaultExportFrameContract(
      digitorCurrentRendererBackendForExport(),
    );
    final resolvedWorkingFormat =
        workingFormat ??
        currentPreviewContract?.workingFormat ??
        defaultFrameContract.workingFormat;
    final resolvedColorMetadata =
        colorMetadata ??
        currentPreviewContract?.colorMetadata ??
        defaultFrameContract.colorMetadata;
    if (snapshotIdentity <= 0 ||
        timelineRevision <= 0 ||
        renderRevision <= 0 ||
        nodeGraphRevision <= 0 ||
        colorPipelineRevision <= 0 ||
        audioRevision <= 0 ||
        graphRecipeIdentity.isEmpty ||
        resolvedColorMetadata.isEmpty ||
        fpsNum <= 0 ||
        fpsDen <= 0 ||
        videoBitrate <= 0) {
      throw ArgumentError('Frozen timeline export metadata is incomplete.');
    }
    final frameCount = lastFrame - firstFrame + 1;
    final durationUs = frameCount * 1000000 * fpsDen ~/ fpsNum;
    if (durationUs <= 0) {
      throw ArgumentError('Frozen timeline export duration must be positive.');
    }

    final nativePath = path.toNativeUtf8();
    final nativeColorMetadata = resolvedColorMetadata.toNativeUtf8();
    final nativeGraphIdentity = graphRecipeIdentity.toNativeUtf8();
    final request = calloc<DigitorFlutterExportRequestV2Native>();
    NativeCallable<DigitorProgressNative>? callback;
    try {
      request.ref
        ..structSize = sizeOf<DigitorFlutterExportRequestV2Native>()
        ..apiVersion = 2
        ..outputPath = nativePath
        ..format = format.index
        ..codec = codec.index
        ..firstFrame = firstFrame
        ..lastFrame = lastFrame
        ..width = width
        ..height = height
        ..snapshotIdentity = snapshotIdentity
        ..timelineRevision = timelineRevision
        ..renderRevision = renderRevision
        ..nodeGraphRevision = nodeGraphRevision
        ..colorPipelineRevision = colorPipelineRevision
        ..audioRevision = audioRevision
        ..workingFormat = resolvedWorkingFormat
        ..alphaPolicy = 1
        ..fpsNum = fpsNum
        ..fpsDen = fpsDen
        ..durationUs = durationUs
        ..videoBitrate = videoBitrate
        ..variableFrameRate = 0
        ..hdr = hdr ? 1 : 0
        ..tenBit = tenBit ? 1 : 0
        ..reserved0 = 0
        ..colorMetadata = nativeColorMetadata
        ..graphRecipeIdentity = nativeGraphIdentity;
      if (onProgress != null) {
        callback = NativeCallable<DigitorProgressNative>.listener((
          double fraction,
          int completed,
          int total,
          Pointer<Void> _,
        ) {
          onProgress(
            DigitorExportProgress(
              fraction: fraction,
              completed: completed,
              total: total,
            ),
          );
        });
      }
      _check(
        'timelineExportV2',
        digitorFlutterProductionTimelineExportV2(
          _graph.nativeHandle,
          _graph.graphRevision,
          _graph.parameterRevision,
          request,
          callback?.nativeFunction ?? nullptr,
          nullptr,
        ),
      );
    } finally {
      callback?.close();
      calloc.free(request);
      calloc.free(nativeGraphIdentity);
      calloc.free(nativeColorMetadata);
      calloc.free(nativePath);
    }
  }

  void cancel() {
    if (_closed || !configured) return;
    _check('timelineCancel', digitorFlutterProductionTimelineCancel());
  }

  void close() {
    if (_closed) return;
    clear();
    _closed = true;
  }

  String get lastError {
    if (_closed) return '';
    final size = calloc<Uint32>();
    try {
      if (digitorFlutterProductionTimelineGetLastError(nullptr, size) != 0 ||
          size.value == 0) {
        return '';
      }
      final buffer = calloc<Uint8>(size.value);
      try {
        if (digitorFlutterProductionTimelineGetLastError(buffer, size) != 0) {
          return '';
        }
        return buffer.cast<Utf8>().toDartString();
      } finally {
        calloc.free(buffer);
      }
    } finally {
      calloc.free(size);
    }
  }

  void _ensureOpen() {
    if (_closed) {
      throw StateError('Production timeline session is closed.');
    }
  }

  void _ensureConfigured() {
    _ensureOpen();
    if (!configured) {
      throw StateError('Configure the native production timeline first.');
    }
  }

  void _check(String operation, int result) {
    if (result != 0) {
      throw DigitorProductionException(operation, result, lastError);
    }
  }
}
