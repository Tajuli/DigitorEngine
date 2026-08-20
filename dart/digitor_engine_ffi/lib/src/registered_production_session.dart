import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'engine.dart';
import 'native_api.dart';
import 'native_production_api.dart';
import 'node_graph.dart';
import 'production.dart';

final class DigitorDefaultExportFrameContract {
  const DigitorDefaultExportFrameContract({
    required this.workingFormat,
    required this.colorMetadata,
  });

  final int workingFormat;
  final String colorMetadata;
}

/// Resolves the default frozen frame contract for a renderer.
///
/// D3D12 production media is decoded/processed as canonical RGBA32F while
/// the color-space identity remains the backend-independent `linear-rgba`.
/// Pixel precision belongs to [workingFormat], not to the color metadata
/// string. Once a preview is rendered, export freezes that exact processed GPU
/// format so strict preview/export parity does not depend on a backend-wide
/// format guess.
DigitorDefaultExportFrameContract digitorDefaultExportFrameContract(
  int rendererBackend,
) {
  if (rendererBackend == DigitorBackend.direct3D12.nativeValue) {
    return DigitorDefaultExportFrameContract(
      workingFormat: DigitorPixelFormat.rgba32Float.nativeValue,
      colorMetadata: 'linear-rgba',
    );
  }
  return DigitorDefaultExportFrameContract(
    workingFormat: DigitorPixelFormat.rgba16Float.nativeValue,
    colorMetadata: 'linear-rgba',
  );
}

DigitorDefaultExportFrameContract _previewFrameContract(
  DigitorPixelFormat format,
) {
  if (format == DigitorPixelFormat.rgba32Float) {
    return DigitorDefaultExportFrameContract(
      workingFormat: format.nativeValue,
      colorMetadata: 'linear-rgba',
    );
  }
  if (format == DigitorPixelFormat.rgba16Float) {
    return DigitorDefaultExportFrameContract(
      workingFormat: format.nativeValue,
      colorMetadata: 'linear-rgba',
    );
  }
  throw StateError(
    'Production preview returned an unsupported export working format: ${format.name}.',
  );
}

int digitorCurrentRendererBackendForExport() {
  final renderer = calloc<DigitorRendererInfoNative>();
  try {
    final result = digitorGetRendererInfo(renderer);
    if (result != 0) {
      throw DigitorProductionException('getRendererInfoForExport', result);
    }
    return renderer.ref.backend;
  } finally {
    calloc.free(renderer);
  }
}

/// Package-internal production session that resolves the native host installed
/// by the Flutter platform plugin. Applications never provide callback pointers.
final class DigitorRegisteredProductionSession {
  DigitorRegisteredProductionSession._(this._handle, this._graph);

  static bool get hostRegistered =>
      digitorFlutterProductionHostRegistered() != 0;

  factory DigitorRegisteredProductionSession.open({
    required String mediaPath,
    required DigitorNodeGraph nodeGraph,
  }) {
    if (mediaPath.isEmpty) {
      throw ArgumentError.value(mediaPath, 'mediaPath');
    }
    if (!hostRegistered) {
      throw StateError(
        'The native Flutter production host is not registered for this platform.',
      );
    }
    final path = mediaPath.toNativeUtf8();
    final out = calloc<Pointer<DigitorFlutterProductionSessionNative>>();
    try {
      final result = digitorFlutterProductionCreateRegistered(path, out);
      if (result != 0 || out.value == nullptr) {
        throw DigitorProductionException(
          'createRegistered',
          result == 0 ? 100 : result,
        );
      }
      final session = DigitorRegisteredProductionSession._(
        out.value,
        nodeGraph,
      );
      nodeGraph.retainForProductionSession();
      try {
        session.bindNodeGraph();
      } catch (_) {
        nodeGraph.releaseFromProductionSession();
        digitorFlutterProductionDestroy(out.value);
        rethrow;
      }
      return session;
    } finally {
      calloc.free(path);
      calloc.free(out);
    }
  }

  Pointer<DigitorFlutterProductionSessionNative> _handle;
  final DigitorNodeGraph _graph;
  int? _outstandingPreviewGeneration;
  DigitorDefaultExportFrameContract? _lastPreviewFrameContract;
  int? _lastPreviewGraphRevision;
  int? _lastPreviewParameterRevision;
  bool _disposed = false;

  void bindNodeGraph() {
    _ensureAlive();
    if (_outstandingPreviewGeneration != null) {
      throw StateError(
        'Consume the current preview before rebinding the graph.',
      );
    }
    _check(
      'bindNodeGraph',
      digitorFlutterProductionBindNodeGraph(
        _handle,
        _graph.nativeHandle,
        _graph.graphRevision,
        _graph.parameterRevision,
      ),
    );
  }

  DigitorPreviewCapabilities get previewCapabilities {
    _ensureAlive();
    final native = calloc<DigitorNativePreviewCapabilitiesNative>();
    try {
      native.ref.structSize = sizeOf<DigitorNativePreviewCapabilitiesNative>();
      _check(
        'queryPreview',
        digitorFlutterProductionQueryPreview(_handle, native),
      );
      final value = native.ref;
      final strictMode =
          value.selectedMode == DigitorPreviewMode.nativeGpuStrict.nativeValue;
      final selectedMode = strictMode
          ? DigitorPreviewMode.nativeGpuStrict
          : DigitorPreviewMode.compatibility;
      return DigitorPreviewCapabilities(
        nativeGpuPreviewAvailable: value.nativeGpuPreviewAvailable != 0,
        trueSharedResourceZeroCopy: value.trueSharedResourceZeroCopy != 0,
        gpuToGpuCopy: value.gpuToGpuCopy != 0,
        cpuFallbackOnly: value.cpuFallbackOnly != 0,
        sdrSupported: value.sdrSupported != 0,
        hdrSupported: value.hdrSupported != 0,
        protectedContentSupported: value.protectedContentSupported != 0,
        resizeSupported: value.resizeSupported != 0,
        backend: DigitorNativeTextureBackend.fromNative(value.backend),
        handleType: DigitorNativeTextureHandleType.fromNative(value.handleType),
        supportedPixelFormats: value.supportedPixelFormats,
        selectedMode: selectedMode,
        reasonUnavailable: _readInt8Array(value.reasonUnavailable, 192),
      );
    } finally {
      calloc.free(native);
    }
  }

  void setPreviewTarget({
    required int nativeTargetHandle,
    required int width,
    required int height,
    required DigitorNativeTextureHandleType handleType,
  }) {
    _ensureAlive();
    if (nativeTargetHandle == 0 || width <= 0 || height <= 0) {
      throw ArgumentError(
        'A live native preview target and dimensions are required.',
      );
    }
    final target = calloc<DigitorFlutterPreviewTargetNative>();
    try {
      target.ref
        ..structSize = sizeOf<DigitorFlutterPreviewTargetNative>()
        ..apiVersion = 1
        ..nativeTargetHandle = nativeTargetHandle
        ..width = width
        ..height = height
        ..handleType = handleType.nativeValue;
      _check(
        'setPreviewTarget',
        digitorFlutterProductionSetPreviewTarget(_handle, target),
      );
    } finally {
      calloc.free(target);
    }
  }

  DigitorNativeGpuTextureFrame preview({
    required int timestampUs,
    required int width,
    required int height,
  }) {
    _ensureAlive();
    if (_outstandingPreviewGeneration != null) {
      throw StateError(
        'Consume the current preview before requesting another.',
      );
    }
    bindNodeGraph();
    final native = calloc<DigitorNativeGpuTextureDescriptorNative>();
    try {
      native.ref
        ..structSize = sizeOf<DigitorNativeGpuTextureDescriptorNative>()
        ..apiVersion = 1;
      _check(
        'preview',
        digitorFlutterProductionPreview(
          _handle,
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
      _lastPreviewFrameContract = _previewFrameContract(frame.pixelFormat);
      _lastPreviewGraphRevision = _graph.graphRevision;
      _lastPreviewParameterRevision = _graph.parameterRevision;
      _outstandingPreviewGeneration = frame.generation;
      return frame;
    } finally {
      calloc.free(native);
    }
  }

  void previewConsumed([int? generation]) {
    _ensureAlive();
    final expected = _outstandingPreviewGeneration;
    if (expected == null) return;
    final resolved = generation ?? expected;
    if (resolved != expected) {
      throw ArgumentError.value(generation, 'generation');
    }
    _check(
      'previewConsumed',
      digitorFlutterProductionPreviewConsumed(_handle, resolved),
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
    _ensureAlive();

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
    bindNodeGraph();
    if (firstFrame < 0 || lastFrame < firstFrame || width <= 0 || height <= 0) {
      throw ArgumentError('Invalid frozen export frame range or dimensions.');
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
      throw ArgumentError('Frozen export V2 metadata is incomplete.');
    }
    final frameCount = lastFrame - firstFrame + 1;
    final durationUs = frameCount * 1000000 * fpsDen ~/ fpsNum;
    if (durationUs <= 0) {
      throw ArgumentError('Frozen export duration must be positive.');
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
        'exportV2',
        digitorFlutterProductionExportV2(
          _handle,
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
    _ensureAlive();
    _check('cancel', digitorFlutterProductionCancel(_handle));
  }

  void dispose() {
    if (_disposed) return;
    previewConsumed();
    _check('destroy', digitorFlutterProductionDestroy(_handle));
    _graph.releaseFromProductionSession();
    _handle = nullptr;
    _disposed = true;
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) {
      throw StateError('Production session is disposed.');
    }
  }

  String get lastError {
    _ensureAlive();
    final size = calloc<Uint32>();
    try {
      if (digitorFlutterProductionGetLastError(_handle, nullptr, size) != 0 ||
          size.value == 0) {
        return '';
      }
      final buffer = calloc<Uint8>(size.value);
      try {
        if (digitorFlutterProductionGetLastError(_handle, buffer, size) != 0) {
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

  void _check(String operation, int result) {
    if (result != 0) {
      final diagnostic = _disposed || _handle == nullptr ? '' : lastError;
      throw DigitorProductionException(operation, result, diagnostic);
    }
  }

  static String _readInt8Array(Array<Int8> value, int length) {
    final bytes = <int>[];
    for (var i = 0; i < length; i++) {
      final byte = value[i];
      if (byte == 0) break;
      bytes.add(byte & 0xff);
    }
    return String.fromCharCodes(bytes);
  }
}
