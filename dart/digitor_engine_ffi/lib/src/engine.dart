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

enum DigitorExportFormat { mp4, mov, mkv, pngSequence, tiffSequence, exrSequence }
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
  final DigitorBackend backend;
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
/// hook. Call [initialize] once near app startup, create sessions from this
/// instance, and [close] it during app shutdown.
final class DigitorEngine {
  DigitorEngine._(this._ownsInitialization);

  static String get version {
    final pointer = digitorGetVersion();
    if (pointer == nullptr) {
      throw const DigitorEngineException('getVersion', 100);
    }
    return pointer.toDartString();
  }

  static DigitorEngine initialize({
    DigitorBackend preferredBackend = DigitorBackend.automatic,
    bool enableValidation = false,
    bool allowCpuFallback = true,
  }) {
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
      return DigitorEngine._(result == 0);
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
  /// The current native C API reports whether a production native-GPU Flutter
  /// presenter is bound through [DigitorSession.previewCapabilities]. Until it
  /// is, [DigitorSession.preview] is the explicit CPU-readable compatibility
  /// path and must not be described as zero-copy GPU presentation.
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
      final mode = value.selectedMode == DigitorPreviewMode.nativeGpuStrict.nativeValue
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
        backend: DigitorBackend.fromNative(value.backend),
        selectedMode: mode,
        reasonUnavailable: _readInt8Array(value.reasonUnavailable, 192),
      );
    } finally {
      calloc.free(native);
    }
  }

  Future<void> seek(int frame) {
    if (frame < 0) throw ArgumentError.value(frame, 'frame');
    return _runCompletion(
      'seek',
      (callback) => digitorSdkSeekAsync(_handle, frame, callback, nullptr),
    );
  }

  Future<DigitorPreviewFrame> preview({
    required int frame,
    required int width,
    required int height,
  }) async {
    if (frame < 0) throw ArgumentError.value(frame, 'frame');
    if (width <= 0) throw ArgumentError.value(width, 'width');
    if (height <= 0) throw ArgumentError.value(height, 'height');

    await _runCompletion(
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
      _check('getNativeTexture', digitorSdkGetNativeTexture(_handle, texture));
      final value = texture.ref;
      if (value.pixels == nullptr || value.rowBytes <= 0 || value.height <= 0) {
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

    _ensureAlive();
    _ensureIdle();
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
    var completed = false;
    completion = NativeCallable<DigitorCompletionNative>.listener(
      (int result, Pointer<Void> _) {
        if (completed) return;
        completed = true;
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
      completed = true;
      completion.close();
      progressCallback?.close();
      calloc.free(nativePath);
      throw DigitorEngineException('export', immediate);
    }

    return _track(completer.future);
  }

  void cancel() {
    _ensureAlive();
    _check('cancel', digitorSdkCancel(_handle));
  }

  Future<void> dispose() async {
    if (_disposed) return;
    final active = _activeOperation;
    if (active != null) {
      final result = digitorSdkCancel(_handle);
      if (result != 0) {
        throw DigitorEngineException('cancelBeforeDispose', result);
      }
      try {
        await active;
      } on DigitorEngineException {
        // Cancellation/failure is an expected terminal state while disposing.
      }
    }
    _check('sdkDestroy', digitorSdkDestroy(_handle));
    _handle = nullptr;
    _disposed = true;
    _onDisposed();
  }

  Future<void> _runCompletion(
    String operation,
    int Function(Pointer<NativeFunction<DigitorCompletionNative>>) invoke,
  ) {
    _ensureAlive();
    _ensureIdle();
    final completer = Completer<void>();
    late final NativeCallable<DigitorCompletionNative> callback;
    var completed = false;
    callback = NativeCallable<DigitorCompletionNative>.listener(
      (int result, Pointer<Void> _) {
        if (completed) return;
        completed = true;
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
      completed = true;
      callback.close();
      throw DigitorEngineException(operation, immediate);
    }
    return _track(completer.future);
  }

  Future<void> _track(Future<void> operation) {
    late final Future<void> tracked;
    tracked = operation.whenComplete(() {
      if (identical(_activeOperation, tracked)) {
        _activeOperation = null;
      }
    });
    _activeOperation = tracked;
    return tracked;
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
