import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'engine.dart';
import 'native_api.dart';
import 'native_production_api.dart';
import 'node_graph.dart';

final class DigitorProductionException implements Exception {
  const DigitorProductionException(
    this.operation,
    this.resultCode, [
    this.diagnostic = '',
  ]);

  final String operation;
  final int resultCode;
  final String diagnostic;

  @override
  String toString() => diagnostic.isEmpty
      ? 'DigitorProductionException(operation: $operation, result: $resultCode)'
      : 'DigitorProductionException(operation: $operation, result: $resultCode, diagnostic: $diagnostic)';
}

enum DigitorPixelFormat {
  rgba32Float(1),
  rgba8Unorm(2),
  bgra8Unorm(3),
  rgba16Float(4);

  const DigitorPixelFormat(this.nativeValue);
  final int nativeValue;

  static DigitorPixelFormat fromNative(int value) => values.firstWhere(
    (item) => item.nativeValue == value,
    orElse: () => rgba32Float,
  );
}

enum DigitorNativeTextureReadiness {
  notReady(0),
  ready(1),
  deviceLost(2);

  const DigitorNativeTextureReadiness(this.nativeValue);
  final int nativeValue;

  static DigitorNativeTextureReadiness fromNative(int value) => values
      .firstWhere((item) => item.nativeValue == value, orElse: () => notReady);
}

/// Native GPU texture returned by the production preview path.
///
/// The descriptor remains borrowed from [DigitorProductionSession] until
/// [DigitorProductionSession.previewConsumed] is called for its generation.
final class DigitorNativeGpuTextureFrame {
  const DigitorNativeGpuTextureFrame({
    required this.backend,
    required this.handleType,
    required this.nativeHandle,
    required this.secondaryHandle,
    required this.width,
    required this.height,
    required this.pixelFormat,
    required this.alphaMode,
    required this.colorPrimaries,
    required this.transferFunction,
    required this.matrixCoefficients,
    required this.colorRange,
    required this.timestampUs,
    required this.generation,
    required this.deviceIdentity,
    required this.contextIdentity,
    required this.acquireSyncHandle,
    required this.acquireSyncValue,
    required this.releaseSyncHandle,
    required this.releaseSyncValue,
    required this.ownershipToken,
    required this.protectedContent,
    required this.readiness,
  });

  final DigitorNativeTextureBackend backend;
  final DigitorNativeTextureHandleType handleType;
  final int nativeHandle;
  final int secondaryHandle;
  final int width;
  final int height;
  final DigitorPixelFormat pixelFormat;
  final int alphaMode;
  final int colorPrimaries;
  final int transferFunction;
  final int matrixCoefficients;
  final int colorRange;
  final int timestampUs;
  final int generation;
  final int deviceIdentity;
  final int contextIdentity;
  final int acquireSyncHandle;
  final int acquireSyncValue;
  final int releaseSyncHandle;
  final int releaseSyncValue;
  final int ownershipToken;
  final bool protectedContent;
  final DigitorNativeTextureReadiness readiness;
}

/// Native callbacks supplied by the platform Flutter embedding layer.
///
/// The platform host owns real decode/import, renderer-context interop and
/// Flutter texture registration. Once a GPU backend has been selected these
/// callbacks must surface failures rather than substituting CPU pixels.
final class DigitorProductionHost {
  const DigitorProductionHost({
    required this.openMedia,
    required this.renderFrame,
    required this.exportMedia,
    required this.queryPreview,
    required this.cancel,
    required this.closeMedia,
    required this.releaseTexture,
    this.userData,
    this.requiredDeviceIdentity = 0,
    this.requiredContextIdentity = 0,
  });

  final Pointer<Void>? userData;
  final int requiredDeviceIdentity;
  final int requiredContextIdentity;
  final Pointer<Void> openMedia;
  final Pointer<Void> renderFrame;
  final Pointer<Void> exportMedia;
  final Pointer<Void> queryPreview;
  final Pointer<Void> cancel;
  final Pointer<Void> closeMedia;
  final Pointer<Void> releaseTexture;

  void writeTo(Pointer<DigitorFlutterProductionHostNative> native) {
    if (openMedia == nullptr ||
        renderFrame == nullptr ||
        exportMedia == nullptr ||
        queryPreview == nullptr ||
        cancel == nullptr ||
        closeMedia == nullptr ||
        releaseTexture == nullptr) {
      throw ArgumentError('All production host callbacks are required.');
    }
    native.ref
      ..structSize = sizeOf<DigitorFlutterProductionHostNative>()
      ..apiVersion = 1
      ..userData = userData ?? nullptr
      ..requiredDeviceIdentity = requiredDeviceIdentity
      ..requiredContextIdentity = requiredContextIdentity
      ..openMedia = openMedia
          .cast<NativeFunction<DigitorFlutterOpenMediaNative>>()
      ..renderFrame = renderFrame
          .cast<NativeFunction<DigitorFlutterRenderFrameNative>>()
      ..exportMedia = exportMedia
          .cast<NativeFunction<DigitorFlutterExportMediaNative>>()
      ..queryPreview = queryPreview
          .cast<NativeFunction<DigitorFlutterQueryPreviewNative>>()
      ..cancel = cancel.cast<NativeFunction<DigitorFlutterCancelNative>>()
      ..closeMedia = closeMedia
          .cast<NativeFunction<DigitorFlutterCloseMediaNative>>()
      ..releaseTexture = releaseTexture
          .cast<NativeFunction<DigitorFlutterReleaseTextureNative>>();
  }
}

/// Production media bridge shared by preview and export.
///
/// Unlike the compatibility [DigitorSession], this class never exposes a CPU
/// preview buffer. The same bound [DigitorNodeGraph] and immutable revisions are
/// supplied to both production preview and production export callbacks.
final class DigitorProductionSession {
  DigitorProductionSession._(this._handle);

  factory DigitorProductionSession.open({
    required DigitorProductionHost host,
    required String mediaPath,
    DigitorNodeGraph? nodeGraph,
  }) {
    if (mediaPath.isEmpty) {
      throw ArgumentError.value(mediaPath, 'mediaPath');
    }
    final nativeHost = calloc<DigitorFlutterProductionHostNative>();
    final path = mediaPath.toNativeUtf8();
    final out = calloc<Pointer<DigitorFlutterProductionSessionNative>>();
    try {
      host.writeTo(nativeHost);
      final result = digitorFlutterProductionCreate(nativeHost, path, out);
      if (result != 0 || out.value == nullptr) {
        throw DigitorProductionException('create', result == 0 ? 100 : result);
      }
      final session = DigitorProductionSession._(out.value);
      if (nodeGraph != null) {
        session.bindNodeGraph(nodeGraph);
      }
      return session;
    } finally {
      calloc.free(nativeHost);
      calloc.free(path);
      calloc.free(out);
    }
  }

  Pointer<DigitorFlutterProductionSessionNative> _handle;
  DigitorNodeGraph? _graph;
  bool _disposed = false;
  int? _outstandingPreviewGeneration;

  DigitorNodeGraph? get nodeGraph => _graph;

  /// Binds or refreshes the exact graph revisions used by future frames/jobs.
  void bindNodeGraph(DigitorNodeGraph graph) {
    _ensureAlive();
    if (_outstandingPreviewGeneration != null) {
      throw StateError(
        'Consume the current preview before rebinding the node graph.',
      );
    }
    if (_graph != null && !identical(_graph, graph)) {
      throw StateError(
        'A production session is pinned to one node graph. '
        'Create a new session to use a different graph.',
      );
    }
    _check(
      'bindNodeGraph',
      digitorFlutterProductionBindNodeGraph(
        _handle,
        graph.nativeHandle,
        graph.graphRevision,
        graph.parameterRevision,
      ),
    );
    if (!identical(_graph, graph)) {
      _graph?.releaseFromProductionSession();
      graph.retainForProductionSession();
      _graph = graph;
    }
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
      final selectedMode =
          value.selectedMode == DigitorPreviewMode.nativeGpuStrict.nativeValue
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

  DigitorNativeGpuTextureFrame preview({
    required int timestampUs,
    required int width,
    required int height,
  }) {
    _ensureAlive();
    if (_graph == null) {
      throw StateError('Bind a node graph before preview.');
    }
    if (_outstandingPreviewGeneration != null) {
      throw StateError(
        'Call previewConsumed before requesting the next frame.',
      );
    }
    if (timestampUs < 0) {
      throw ArgumentError.value(timestampUs, 'timestampUs');
    }
    if (width <= 0) {
      throw ArgumentError.value(width, 'width');
    }
    if (height <= 0) {
      throw ArgumentError.value(height, 'height');
    }

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
      _outstandingPreviewGeneration = frame.generation;
      return frame;
    } finally {
      calloc.free(native);
    }
  }

  void previewConsumed([int? generation]) {
    _ensureAlive();
    final expected = _outstandingPreviewGeneration;
    if (expected == null) {
      throw StateError('There is no outstanding preview frame.');
    }
    final resolved = generation ?? expected;
    if (resolved != expected) {
      throw ArgumentError.value(
        generation,
        'generation',
        'Must match the current preview generation.',
      );
    }
    _check(
      'previewConsumed',
      digitorFlutterProductionPreviewConsumed(_handle, resolved),
    );
    _outstandingPreviewGeneration = null;
  }

  /// Runs the platform production export synchronously with the bound recipe.
  ///
  /// The platform host must not invoke the progress callback after this method
  /// returns.
  void export({
    required String path,
    required int firstFrame,
    required int lastFrame,
    required int width,
    required int height,
    DigitorExportFormat format = DigitorExportFormat.mp4,
    DigitorVideoCodec codec = DigitorVideoCodec.h264,
    void Function(DigitorExportProgress progress)? onProgress,
  }) {
    _ensureAlive();
    if (_graph == null) {
      throw StateError('Bind a node graph before export.');
    }
    if (_outstandingPreviewGeneration != null) {
      throw StateError('Consume the current preview before starting export.');
    }
    if (path.isEmpty) {
      throw ArgumentError.value(path, 'path');
    }
    if (firstFrame < 0) {
      throw ArgumentError.value(firstFrame, 'firstFrame');
    }
    if (lastFrame < firstFrame) {
      throw ArgumentError.value(lastFrame, 'lastFrame');
    }
    if (width <= 0) {
      throw ArgumentError.value(width, 'width');
    }
    if (height <= 0) {
      throw ArgumentError.value(height, 'height');
    }

    final nativePath = path.toNativeUtf8();
    final request = calloc<DigitorFlutterExportRequestNative>();
    NativeCallable<DigitorProgressNative>? callback;
    try {
      request.ref
        ..structSize = sizeOf<DigitorFlutterExportRequestNative>()
        ..apiVersion = 1
        ..outputPath = nativePath
        ..format = format.index
        ..codec = codec.index
        ..firstFrame = firstFrame
        ..lastFrame = lastFrame
        ..width = width
        ..height = height;
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
        'export',
        digitorFlutterProductionExport(
          _handle,
          request,
          callback?.nativeFunction ?? nullptr,
          nullptr,
        ),
      );
    } finally {
      callback?.close();
      calloc.free(request);
      calloc.free(nativePath);
    }
  }

  void cancel() {
    _ensureAlive();
    _check('cancel', digitorFlutterProductionCancel(_handle));
  }

  void dispose() {
    if (_disposed) {
      return;
    }
    _check('destroy', digitorFlutterProductionDestroy(_handle));
    _graph?.releaseFromProductionSession();
    _graph = null;
    _handle = nullptr;
    _outstandingPreviewGeneration = null;
    _disposed = true;
  }

  String get lastError {
    _ensureAlive();
    final size = calloc<Uint32>();
    try {
      final first = digitorFlutterProductionGetLastError(
        _handle,
        nullptr,
        size,
      );
      if (first != 0 || size.value == 0) {
        return '';
      }
      final buffer = calloc<Uint8>(size.value);
      try {
        final result = digitorFlutterProductionGetLastError(
          _handle,
          buffer,
          size,
        );
        if (result != 0) {
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
    if (result == 0) {
      return;
    }
    final diagnostic = _disposed || _handle == nullptr ? '' : lastError;
    throw DigitorProductionException(operation, result, diagnostic);
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) {
      throw StateError('DigitorProductionSession is disposed.');
    }
  }
}

String _readInt8Array(Array<Int8> array, int length) {
  final bytes = <int>[];
  for (var i = 0; i < length; i++) {
    final value = array[i];
    if (value == 0) {
      break;
    }
    bytes.add(value & 0xff);
  }
  return String.fromCharCodes(bytes);
}
