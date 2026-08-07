import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'native_api.dart';

enum DigitorBackend {
  automatic(0),
  vulkan(1),
  metal(2),
  direct3D12(3),
  openGles(4),
  cpu(100);

  const DigitorBackend(this.nativeValue);

  final int nativeValue;

  static DigitorBackend fromNative(int value) => values.firstWhere(
        (backend) => backend.nativeValue == value,
        orElse: () => automatic,
      );
}

enum DigitorPreviewMode {
  compatibility(0),
  nativeGpuStrict(1);

  const DigitorPreviewMode(this.nativeValue);

  final int nativeValue;
}

enum DigitorNativeTextureBackend {
  none(0),
  d3d11(1),
  d3d12(2),
  vulkan(3),
  metal(4),
  openGles(5),
  androidHardwareBuffer(6),
  cpuRgba8(100);

  const DigitorNativeTextureBackend(this.nativeValue);

  final int nativeValue;

  static DigitorNativeTextureBackend fromNative(int value) => values.firstWhere(
        (backend) => backend.nativeValue == value,
        orElse: () => none,
      );
}

enum DigitorNativeTextureHandleType {
  none(0),
  dxgiSharedHandle(1),
  d3d11Texture(2),
  d3d12Resource(3),
  vkImage(4),
  metalTexture(5),
  cvPixelBuffer(6),
  androidHardwareBuffer(7),
  eglImage(8),
  glTexture(9),
  cpuPointer(100);

  const DigitorNativeTextureHandleType(this.nativeValue);

  final int nativeValue;

  static DigitorNativeTextureHandleType fromNative(int value) =>
      values.firstWhere(
        (type) => type.nativeValue == value,
        orElse: () => none,
      );
}

enum DigitorExportFormat {
  mp4,
  mov,
  mkv,
  pngSequence,
  tiffSequence,
  exrSequence,
}

enum DigitorVideoCodec { h264, h265, av1 }

final class DigitorEngineException implements Exception {
  const DigitorEngineException(this.operation, this.resultCode);

  final String operation;
  final int resultCode;

  @override
  String toString() =>
      'DigitorEngineException(operation: $operation, result: $resultCode)';
}

final class DigitorRendererInfo {
  const DigitorRendererInfo({
    required this.backend,
    required this.backendName,
    required this.deviceName,
    required this.isGpu,
    required this.supportsCompute,
    required this.supportsFp16,
    required this.supportsFp32,
  });

  final DigitorBackend backend;
  final String backendName;
  final String deviceName;
  final bool isGpu;
  final bool supportsCompute;
  final bool supportsFp16;
  final bool supportsFp32;
}

final class DigitorColorControls {
  const DigitorColorControls({
    this.exposure = 0,
    this.contrast = 1,
    this.saturation = 1,
  });

  final double exposure;
  final double contrast;
  final double saturation;
}

final class DigitorPreviewFrame {
  const DigitorPreviewFrame({
    required this.rgba,
    required this.width,
    required this.height,
    required this.rowBytes,
    required this.generation,
  });

  final Uint8List rgba;
  final int width;
  final int height;
  final int rowBytes;
  final int generation;
}

final class DigitorPreviewCapabilities {
  const DigitorPreviewCapabilities({
    required this.nativeGpuPreviewAvailable,
    required this.trueSharedResourceZeroCopy,
    required this.gpuToGpuCopy,
    required this.cpuFallbackOnly,
    required this.sdrSupported,
    required this.hdrSupported,
    required this.protectedContentSupported,
    required this.resizeSupported,
    required this.backend,
    required this.handleType,
    required this.supportedPixelFormats,
    required this.selectedMode,
    required this.reasonUnavailable,
  });

  final bool nativeGpuPreviewAvailable;
  final bool trueSharedResourceZeroCopy;
  final bool gpuToGpuCopy;
  final bool cpuFallbackOnly;
  final bool sdrSupported;
  final bool hdrSupported;
  final bool protectedContentSupported;
  final bool resizeSupported;
  final DigitorNativeTextureBackend backend;
  final DigitorNativeTextureHandleType handleType;
  final int supportedPixelFormats;
  final DigitorPreviewMode selectedMode;
  final String reasonUnavailable;
}

final class DigitorExportProgress {
  const DigitorExportProgress({
    required this.fraction,
    required this.completed,
    required this.total,
  });

  final double fraction;
  final int completed;
  final int total;
}

/// High-level owner for the process-wide DigitorEngine runtime.
///
/// Native binaries are built and bundled automatically by the package build
/// hook. Call [initialize] near app startup, create sessions from this instance,
/// and [close] it during app shutdown.
final class DigitorEngine {
  DigitorEngine._(this._ownsInitialization);

  /// Version of the public Flutter SDK contract in this repository release.
  static const String version = '0.0.1';

  static DigitorEngine? _current;

  /// Initializes the process-wide engine once per Dart isolate.
  ///
  /// Repeated calls return the currently open facade rather than creating a
  /// second owner that could shut the native singleton down prematurely.
  static DigitorEngine initialize({
    DigitorBackend preferredBackend = DigitorBackend.automatic,
    bool enableValidation = false,
    bool allowCpuFallback = true,
  }) {
    final current = _current;
    if (current != null && !current._closed) return current;

    final config = calloc<DigitorEngineConfigNative>();
    try {
      config.ref
        ..preferredBackend = preferredBackend.nativeValue
        ..enableValidation = enableValidation ? 1 : 0
        ..allowCpuFallback = allowCpuFallback ? 1 : 0;
      final result = digitorInitialize(config);
      if (result != 0 && result != 3) {
        throw DigitorEngineException('initialize', result);
      }
      final engine = DigitorEngine._(result == 0);
      _current = engine;
      return engine;
    } finally {
      calloc.free(config);
    }
  }

  final bool _ownsInitialization;
  final Set<DigitorSession> _sessions = <DigitorSession>{};
  bool _closed = false;

  DigitorRendererInfo get rendererInfo {
    _ensureOpen();
    final native = calloc<DigitorRendererInfoNative>();
    try {
      _check('getRendererInfo', digitorGetRendererInfo(native));
      final info = native.ref;
      return DigitorRendererInfo(
        backend: DigitorBackend.fromNative(info.backend),
        backendName: _readInt8Array(info.backendName, 64),
        deviceName: _readInt8Array(info.deviceName, 128),
        isGpu: info.isGpu != 0,
        supportsCompute: info.supportsCompute != 0,
        supportsFp16: info.supportsFp16 != 0,
        supportsFp32: info.supportsFp32 != 0,
      );
    } finally {
      calloc.free(native);
    }
  }

  /// Creates the Flutter-safe asynchronous compatibility SDK session.
  ///
  /// Query [DigitorSession.previewCapabilities] before attempting a strict
  /// native-GPU presentation path. [DigitorSession.preview] intentionally
  /// returns a Dart-owned copy of the CPU-readable compatibility buffer.
  DigitorSession createSession() {
    _ensureOpen();
    final session = DigitorSession._create(() {});
    session._onDisposed = () => _sessions.remove(session);
    _sessions.add(session);
    return session;
  }

  Future<void> close() async {
    if (_closed) return;
    for (final session in _sessions.toList(growable: false)) {
      await session.dispose();
    }
    if (_ownsInitialization) {
      _check('shutdown', digitorShutdown());
    }
    _closed = true;
    if (identical(_current, this)) _current = null;
  }

  void _ensureOpen() {
    if (_closed) throw StateError('DigitorEngine is closed.');
  }
}

final class DigitorSession {
  DigitorSession._(this._handle, this._onDisposed);

  static DigitorSession _create(void Function() onDisposed) {
    final out = calloc<Pointer<DigitorSdkSessionNative>>();
    try {
      _check('sdkCreate', digitorSdkCreate(out));
      if (out.value == nullptr) {
        throw const DigitorEngineException('sdkCreate', 100);
      }
      return DigitorSession._(out.value, onDisposed);
    } finally {
      calloc.free(out);
    }
  }

  Pointer<DigitorSdkSessionNative> _handle;
  void Function() _onDisposed;
  Future<void>? _activeOperation;
  bool _disposed = false;

  void setColor(DigitorColorControls controls) {
    _ensureAlive();
    _ensureIdle();
    final native = calloc<DigitorColorControlsNative>();
    try {
      native.ref
        ..exposure = controls.exposure
        ..contrast = controls.contrast
        ..saturation = controls.saturation;
      _check('setColor', digitorSdkSetColor(_handle, native.ref));
    } finally {
      calloc.free(native);
    }
  }

  void setPreviewMode(DigitorPreviewMode mode) {
    _ensureAlive();
    _ensureIdle();
    _check(
      'setPreviewMode',
      digitorSdkSetPreviewMode(_handle, mode.nativeValue),
    );
  }

  DigitorPreviewCapabilities get previewCapabilities {
    _ensureAlive();
    final native = calloc<DigitorNativePreviewCapabilitiesNative>();
    try {
      native.ref.structSize = sizeOf<DigitorNativePreviewCapabilitiesNative>();
      _check(
        'queryNativePreview',
        digitorSdkQueryNativePreview(_handle, native),
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

  Future<void> seek(int frame) {
    if (frame < 0) throw ArgumentError.value(frame, 'frame');
    return _track(
      () => _invokeCompletion(
        'seek',
        (callback) => digitorSdkSeekAsync(
          _handle,
          frame,
          callback,
          nullptr,
        ),
      ),
    );
  }

  Future<DigitorPreviewFrame> preview({
    required int frame,
    required int width,
    required int height,
  }) {
    if (frame < 0) throw ArgumentError.value(frame, 'frame');
    if (width <= 0) throw ArgumentError.value(width, 'width');
    if (height <= 0) throw ArgumentError.value(height, 'height');

    return _track(() async {
      await _invokeCompletion(
        'preview',
        (callback) => digitorSdkPreviewAsync(
          _handle,
          frame,
          width,
          height,
          callback,
          nullptr,
        ),
      );

      final texture = calloc<DigitorNativeTextureNative>();
      try {
        _check(
          'getNativeTexture',
          digitorSdkGetNativeTexture(_handle, texture),
        );
        final value = texture.ref;
        if (value.pixels == nullptr ||
            value.rowBytes <= 0 ||
            value.height <= 0) {
          throw const DigitorEngineException('getNativeTexture', 100);
        }
        final byteLength = value.rowBytes * value.height;
        final rgba = Uint8List.fromList(
          value.pixels.cast<Uint8>().asTypedList(byteLength),
        );
        return DigitorPreviewFrame(
          rgba: rgba,
          width: value.width,
          height: value.height,
          rowBytes: value.rowBytes,
          generation: value.generation,
        );
      } finally {
        calloc.free(texture);
      }
    });
  }

  Future<void> export({
    required String path,
    required int firstFrame,
    required int lastFrame,
    required int width,
    required int height,
    DigitorExportFormat format = DigitorExportFormat.mp4,
    DigitorVideoCodec codec = DigitorVideoCodec.h264,
    void Function(DigitorExportProgress progress)? onProgress,
  }) {
    if (path.isEmpty) throw ArgumentError.value(path, 'path');
    if (firstFrame < 0) throw ArgumentError.value(firstFrame, 'firstFrame');
    if (lastFrame < firstFrame) {
      throw ArgumentError.value(lastFrame, 'lastFrame');
    }
    if (width <= 0) throw ArgumentError.value(width, 'width');
    if (height <= 0) throw ArgumentError.value(height, 'height');

    return _track(
      () => _startExport(
        path: path,
        firstFrame: firstFrame,
        lastFrame: lastFrame,
        width: width,
        height: height,
        format: format,
        codec: codec,
        onProgress: onProgress,
      ),
    );
  }

  void cancel() {
    _ensureAlive();
    _check('cancel', digitorSdkCancel(_handle));
  }

  Future<void> dispose() async {
    if (_disposed) return;
    final active = _activeOperation;
    if (active != null) {
      _check('cancelBeforeDispose', digitorSdkCancel(_handle));
      await active;
    }
    _check('sdkDestroy', digitorSdkDestroy(_handle));
    _handle = nullptr;
    _disposed = true;
    _onDisposed();
  }

  Future<void> _startExport({
    required String path,
    required int firstFrame,
    required int lastFrame,
    required int width,
    required int height,
    required DigitorExportFormat format,
    required DigitorVideoCodec codec,
    required void Function(DigitorExportProgress progress)? onProgress,
  }) {
    final nativePath = path.toNativeUtf8();
    NativeCallable<DigitorProgressNative>? progressCallback;
    if (onProgress != null) {
      progressCallback = NativeCallable<DigitorProgressNative>.listener(
        (double fraction, int completed, int total, Pointer<Void> _) {
          if (!_disposed) {
            onProgress(
              DigitorExportProgress(
                fraction: fraction,
                completed: completed,
                total: total,
              ),
            );
          }
        },
      );
    }

    final completer = Completer<void>();
    late final NativeCallable<DigitorCompletionNative> completion;
    var finished = false;
    completion = NativeCallable<DigitorCompletionNative>.listener(
      (int result, Pointer<Void> _) {
        if (finished) return;
        finished = true;
        completion.close();
        progressCallback?.close();
        calloc.free(nativePath);
        if (result == 0) {
          completer.complete();
        } else {
          completer.completeError(DigitorEngineException('export', result));
        }
      },
    );

    final Pointer<NativeFunction<DigitorProgressNative>> progressPointer =
        progressCallback?.nativeFunction ?? nullptr;
    final immediate = digitorSdkExportAsync(
      _handle,
      nativePath,
      format.index,
      codec.index,
      firstFrame,
      lastFrame,
      width,
      height,
      progressPointer,
      completion.nativeFunction,
      nullptr,
    );
    if (immediate != 0) {
      finished = true;
      completion.close();
      progressCallback?.close();
      calloc.free(nativePath);
      throw DigitorEngineException('export', immediate);
    }
    return completer.future;
  }

  Future<void> _invokeCompletion(
    String operation,
    int Function(Pointer<NativeFunction<DigitorCompletionNative>>) invoke,
  ) {
    final completer = Completer<void>();
    late final NativeCallable<DigitorCompletionNative> callback;
    var finished = false;
    callback = NativeCallable<DigitorCompletionNative>.listener(
      (int result, Pointer<Void> _) {
        if (finished) return;
        finished = true;
        callback.close();
        if (result == 0) {
          completer.complete();
        } else {
          completer.completeError(DigitorEngineException(operation, result));
        }
      },
    );

    final immediate = invoke(callback.nativeFunction);
    if (immediate != 0) {
      finished = true;
      callback.close();
      throw DigitorEngineException(operation, immediate);
    }
    return completer.future;
  }

  Future<T> _track<T>(Future<T> Function() start) {
    _ensureAlive();
    _ensureIdle();

    final gate = Completer<void>();
    final active = gate.future;
    _activeOperation = active;

    late final Future<T> operation;
    try {
      operation = start();
    } catch (_) {
      if (!gate.isCompleted) gate.complete();
      if (identical(_activeOperation, active)) _activeOperation = null;
      rethrow;
    }

    return operation.whenComplete(() {
      if (!gate.isCompleted) gate.complete();
      if (identical(_activeOperation, active)) _activeOperation = null;
    });
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) {
      throw StateError('DigitorSession is disposed.');
    }
  }

  void _ensureIdle() {
    if (_activeOperation != null) {
      throw StateError('DigitorSession already has an active operation.');
    }
  }
}

void _check(String operation, int result) {
  if (result != 0) throw DigitorEngineException(operation, result);
}

String _readInt8Array(Array<Int8> value, int capacity) {
  final bytes = <int>[];
  for (var i = 0; i < capacity; i++) {
    final byte = value[i];
    if (byte == 0) break;
    bytes.add(byte & 0xff);
  }
  return utf8.decode(bytes, allowMalformed: true);
}
