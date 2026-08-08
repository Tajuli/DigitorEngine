import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'library_loader.dart';

final class DigitorSdkSessionNative extends Opaque {}

typedef _CompletionNative = Void Function(Int32, Pointer<Void>);
typedef _ProgressNative = Void Function(Double, Int64, Int64, Pointer<Void>);
typedef _CreateNative =
    Int32 Function(Pointer<Pointer<DigitorSdkSessionNative>>);
typedef _CreateDart = int Function(Pointer<Pointer<DigitorSdkSessionNative>>);
typedef _DestroyNative = Int32 Function(Pointer<DigitorSdkSessionNative>);
typedef _DestroyDart = int Function(Pointer<DigitorSdkSessionNative>);
typedef _SeekNative =
    Int32 Function(
      Pointer<DigitorSdkSessionNative>,
      Int64,
      Pointer<NativeFunction<_CompletionNative>>,
      Pointer<Void>,
    );
typedef _SeekDart =
    int Function(
      Pointer<DigitorSdkSessionNative>,
      int,
      Pointer<NativeFunction<_CompletionNative>>,
      Pointer<Void>,
    );
typedef _PreviewNative =
    Int32 Function(
      Pointer<DigitorSdkSessionNative>,
      Int64,
      Uint32,
      Uint32,
      Pointer<NativeFunction<_CompletionNative>>,
      Pointer<Void>,
    );
typedef _PreviewDart =
    int Function(
      Pointer<DigitorSdkSessionNative>,
      int,
      int,
      int,
      Pointer<NativeFunction<_CompletionNative>>,
      Pointer<Void>,
    );

final class DigitorNativeEvent {
  const DigitorNativeEvent({
    required this.kind,
    this.resultCode,
    this.fraction,
    this.completed,
    this.total,
  });

  final DigitorNativeEventKind kind;
  final int? resultCode;
  final double? fraction;
  final int? completed;
  final int? total;
}

enum DigitorNativeEventKind {
  seekCompleted,
  previewCompleted,
  exportProgress,
  exportCompleted,
}

final class DigitorSdkCallbackSession {
  DigitorSdkCallbackSession._(this._library, this._handle) {
    _completion = NativeCallable<_CompletionNative>.listener(_onCompletion);
    _progress = NativeCallable<_ProgressNative>.listener(_onProgress);
  }

  factory DigitorSdkCallbackSession.create({String? libraryPath}) {
    final library = DigitorLibraryLoader.open(overridePath: libraryPath);
    final create = library.lookupFunction<_CreateNative, _CreateDart>(
      'digitor_sdk_create',
    );
    final out = calloc<Pointer<DigitorSdkSessionNative>>();
    try {
      final result = create(out);
      if (result != 0 || out.value == nullptr) {
        throw StateError('digitor_sdk_create failed with result $result');
      }
      return DigitorSdkCallbackSession._(library, out.value);
    } finally {
      calloc.free(out);
    }
  }

  final DynamicLibrary _library;
  Pointer<DigitorSdkSessionNative> _handle;
  final StreamController<DigitorNativeEvent> _events =
      StreamController<DigitorNativeEvent>.broadcast();
  late final NativeCallable<_CompletionNative> _completion;
  late final NativeCallable<_ProgressNative> _progress;
  DigitorNativeEventKind _pendingCompletion =
      DigitorNativeEventKind.seekCompleted;
  bool _disposed = false;

  Stream<DigitorNativeEvent> get events => _events.stream;

  void seek(int frame) {
    _ensureAlive();
    _pendingCompletion = DigitorNativeEventKind.seekCompleted;
    final seek = _library.lookupFunction<_SeekNative, _SeekDart>(
      'digitor_sdk_seek_async',
    );
    _check(seek(_handle, frame, _completion.nativeFunction, nullptr), 'seek');
  }

  void preview({required int frame, required int width, required int height}) {
    _ensureAlive();
    _pendingCompletion = DigitorNativeEventKind.previewCompleted;
    final preview = _library.lookupFunction<_PreviewNative, _PreviewDart>(
      'digitor_sdk_preview_async',
    );
    _check(
      preview(
        _handle,
        frame,
        width,
        height,
        _completion.nativeFunction,
        nullptr,
      ),
      'preview',
    );
  }

  void _onCompletion(int result, Pointer<Void> _) {
    if (_disposed) return;
    _events.add(
      DigitorNativeEvent(kind: _pendingCompletion, resultCode: result),
    );
  }

  void _onProgress(double fraction, int completed, int total, Pointer<Void> _) {
    if (_disposed) return;
    _events.add(
      DigitorNativeEvent(
        kind: DigitorNativeEventKind.exportProgress,
        fraction: fraction,
        completed: completed,
        total: total,
      ),
    );
  }

  Future<void> dispose() async {
    if (_disposed) return;
    final destroy = _library.lookupFunction<_DestroyNative, _DestroyDart>(
      'digitor_sdk_destroy',
    );
    _check(destroy(_handle), 'destroy');
    _disposed = true;
    _handle = nullptr;
    _completion.close();
    _progress.close();
    await _events.close();
  }

  void _ensureAlive() {
    if (_disposed || _handle == nullptr) {
      throw StateError('DigitorSdkCallbackSession is disposed.');
    }
  }

  static void _check(int result, String operation) {
    if (result != 0) {
      throw StateError('$operation failed with result $result');
    }
  }
}
