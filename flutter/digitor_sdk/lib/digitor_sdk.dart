import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

final class _DigitorSdkSession extends Opaque {}

final class _DigitorColorControls extends Struct {
  @Float()
  external double exposure;

  @Float()
  external double contrast;

  @Float()
  external double saturation;
}

typedef _AsyncCallbackNative = Void Function(Int32 result, Pointer<Void> userData);
typedef _ExportProgressNative = Void Function(
  Double fraction,
  Int64 completed,
  Int64 total,
  Pointer<Void> userData,
);

class DigitorException implements Exception {
  const DigitorException(this.operation, this.result);

  final String operation;
  final int result;

  @override
  String toString() => 'DigitorException($operation, result=$result)';
}

class _Bindings {
  _Bindings(this.library)
      : getVersion = library.lookupFunction<
            Pointer<Utf8> Function(),
            Pointer<Utf8> Function()>('digitor_get_version'),
        create = library.lookupFunction<
            Int32 Function(Pointer<Pointer<_DigitorSdkSession>>),
            int Function(Pointer<Pointer<_DigitorSdkSession>>)>(
          'digitor_sdk_create',
        ),
        destroy = library.lookupFunction<
            Int32 Function(Pointer<_DigitorSdkSession>),
            int Function(Pointer<_DigitorSdkSession>)>('digitor_sdk_destroy'),
        setColor = library.lookupFunction<
            Int32 Function(Pointer<_DigitorSdkSession>, _DigitorColorControls),
            int Function(Pointer<_DigitorSdkSession>, _DigitorColorControls)>(
          'digitor_sdk_set_color',
        ),
        preview = library.lookupFunction<
            Int32 Function(
              Pointer<_DigitorSdkSession>,
              Int64,
              Uint32,
              Uint32,
              Pointer<NativeFunction<_AsyncCallbackNative>>,
              Pointer<Void>,
            ),
            int Function(
              Pointer<_DigitorSdkSession>,
              int,
              int,
              int,
              Pointer<NativeFunction<_AsyncCallbackNative>>,
              Pointer<Void>,
            )>('digitor_sdk_preview_async'),
        seek = library.lookupFunction<
            Int32 Function(
              Pointer<_DigitorSdkSession>,
              Int64,
              Pointer<NativeFunction<_AsyncCallbackNative>>,
              Pointer<Void>,
            ),
            int Function(
              Pointer<_DigitorSdkSession>,
              int,
              Pointer<NativeFunction<_AsyncCallbackNative>>,
              Pointer<Void>,
            )>('digitor_sdk_seek_async'),
        export = library.lookupFunction<
            Int32 Function(
              Pointer<_DigitorSdkSession>,
              Pointer<Utf8>,
              Int32,
              Int32,
              Int64,
              Int64,
              Uint32,
              Uint32,
              Pointer<NativeFunction<_ExportProgressNative>>,
              Pointer<NativeFunction<_AsyncCallbackNative>>,
              Pointer<Void>,
            ),
            int Function(
              Pointer<_DigitorSdkSession>,
              Pointer<Utf8>,
              int,
              int,
              int,
              int,
              int,
              int,
              Pointer<NativeFunction<_ExportProgressNative>>,
              Pointer<NativeFunction<_AsyncCallbackNative>>,
              Pointer<Void>,
            )>('digitor_sdk_export_async'),
        cancel = library.lookupFunction<
            Int32 Function(Pointer<_DigitorSdkSession>),
            int Function(Pointer<_DigitorSdkSession>)>('digitor_sdk_cancel'),
        productionHostRegistered = library.lookupFunction<
            Int32 Function(),
            int Function()>('digitor_flutter_production_host_registered');

  final DynamicLibrary library;
  final Pointer<Utf8> Function() getVersion;
  final int Function(Pointer<Pointer<_DigitorSdkSession>>) create;
  final int Function(Pointer<_DigitorSdkSession>) destroy;
  final int Function(Pointer<_DigitorSdkSession>, _DigitorColorControls) setColor;
  final int Function(
    Pointer<_DigitorSdkSession>,
    int,
    int,
    int,
    Pointer<NativeFunction<_AsyncCallbackNative>>,
    Pointer<Void>,
  ) preview;
  final int Function(
    Pointer<_DigitorSdkSession>,
    int,
    Pointer<NativeFunction<_AsyncCallbackNative>>,
    Pointer<Void>,
  ) seek;
  final int Function(
    Pointer<_DigitorSdkSession>,
    Pointer<Utf8>,
    int,
    int,
    int,
    int,
    int,
    int,
    Pointer<NativeFunction<_ExportProgressNative>>,
    Pointer<NativeFunction<_AsyncCallbackNative>>,
    Pointer<Void>,
  ) export;
  final int Function(Pointer<_DigitorSdkSession>) cancel;
  final int Function() productionHostRegistered;
}

/// Flutter-safe SDK facade backed by DigitorEngine's real C ABI.
///
/// Long-running native operations are already dispatched by DigitorEngine on a
/// native worker thread. [NativeCallable.listener] marshals completion back to
/// Dart without blocking Flutter's UI isolate. Production zero-copy preview is
/// available only after the platform Flutter plugin has registered the native
/// production host; [productionHostReady] exposes that readiness explicitly.
class DigitorSdk {
  DigitorSdk(this.libraryPath);

  final String libraryPath;
  late final _Bindings _bindings = _Bindings(DynamicLibrary.open(libraryPath));
  Pointer<_DigitorSdkSession>? _session;
  bool _disposed = false;

  Future<int> versionProbe() => Future.sync(() {
        final version = _bindings.getVersion().toDartString();
        return int.tryParse(version.split('.').first) ?? 0;
      });

  String version() => _bindings.getVersion().toDartString();

  bool productionHostReady() => _bindings.productionHostRegistered() != 0;

  Future<void> seek(int frame) => Future.sync(() {
        if (frame < 0) throw ArgumentError.value(frame, 'frame');
        final session = _ensureSession();
        return _runAsync(
          'seek',
          (callback) => _bindings.seek(session, frame, callback, nullptr),
        );
      });

  Future<void> setColor({
    double exposure = 0,
    double contrast = 1,
    double saturation = 1,
  }) async {
    if (![exposure, contrast, saturation].every((value) => value.isFinite)) {
      throw ArgumentError('finite controls required');
    }
    final session = _ensureSession();
    final controls = calloc<_DigitorColorControls>();
    try {
      controls.ref
        ..exposure = exposure
        ..contrast = contrast
        ..saturation = saturation;
      _check('setColor', _bindings.setColor(session, controls.ref));
    } finally {
      calloc.free(controls);
    }
  }

  Future<void> preview(int frame, int width, int height) => Future.sync(() {
        if (frame < 0 || width <= 0 || height <= 0) {
          throw ArgumentError('invalid preview dimensions or frame');
        }
        final session = _ensureSession();
        return _runAsync(
          'preview',
          (callback) =>
              _bindings.preview(session, frame, width, height, callback, nullptr),
        );
      });

  Future<void> export(
    String path, {
    int format = 0,
    int codec = 0,
    int firstFrame = 0,
    int lastFrame = 0,
    int width = 1920,
    int height = 1080,
  }) =>
      Future.sync(() {
        if (path.isEmpty) throw ArgumentError.value(path, 'path');
        if (firstFrame < 0 ||
            lastFrame < firstFrame ||
            width <= 0 ||
            height <= 0) {
          throw ArgumentError('invalid export range or dimensions');
        }
        final session = _ensureSession();
        final nativePath = path.toNativeUtf8();
        return _runAsync(
          'export',
          (callback) => _bindings.export(
            session,
            nativePath,
            format,
            codec,
            firstFrame,
            lastFrame,
            width,
            height,
            nullptr,
            callback,
            nullptr,
          ),
          onComplete: () => calloc.free(nativePath),
        );
      });

  void cancel() {
    final session = _session;
    if (session == null || _disposed) return;
    _check('cancel', _bindings.cancel(session));
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    final session = _session;
    _session = null;
    if (session != null) {
      _check('destroy', _bindings.destroy(session));
    }
  }

  Pointer<_DigitorSdkSession> _ensureSession() {
    if (_disposed) throw StateError('DigitorSdk has been disposed');
    final existing = _session;
    if (existing != null) return existing;

    final out = calloc<Pointer<_DigitorSdkSession>>();
    try {
      _check('create', _bindings.create(out));
      final created = out.value;
      if (created == nullptr) {
        throw const DigitorException('create', 100);
      }
      _session = created;
      return created;
    } finally {
      calloc.free(out);
    }
  }

  Future<void> _runAsync(
    String operation,
    int Function(Pointer<NativeFunction<_AsyncCallbackNative>>) start, {
    void Function()? onComplete,
  }) {
    final completer = Completer<void>();
    late final NativeCallable<_AsyncCallbackNative> callback;
    var completed = false;

    void finish() {
      if (completed) return;
      completed = true;
      callback.close();
      onComplete?.call();
    }

    callback = NativeCallable<_AsyncCallbackNative>.listener((result, _) {
      if (completer.isCompleted) return;
      if (result == 0) {
        completer.complete();
      } else {
        completer.completeError(DigitorException(operation, result));
      }
      finish();
    });

    final startResult = start(callback.nativeFunction);
    if (startResult != 0) {
      finish();
      throw DigitorException(operation, startResult);
    }
    return completer.future;
  }

  static void _check(String operation, int result) {
    if (result != 0) throw DigitorException(operation, result);
  }
}
