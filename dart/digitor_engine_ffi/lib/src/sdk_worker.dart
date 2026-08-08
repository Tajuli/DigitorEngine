import 'dart:async';
import 'dart:ffi';

final class DigitorSdkWorkerHandle extends Opaque {}

typedef _ProgressNative = Void Function(Pointer<Void>, Uint64, Uint64);
typedef _CompletionNative = Void Function(Pointer<Void>, Int32);

typedef _CreateNative =
    Pointer<DigitorSdkWorkerHandle> Function(
      Int32,
      Uint64,
      Pointer<NativeFunction<_ProgressNative>>,
      Pointer<NativeFunction<_CompletionNative>>,
      Pointer<Void>,
    );
typedef _CreateDart =
    Pointer<DigitorSdkWorkerHandle> Function(
      int,
      int,
      Pointer<NativeFunction<_ProgressNative>>,
      Pointer<NativeFunction<_CompletionNative>>,
      Pointer<Void>,
    );
typedef _CommandNative = Int32 Function(Pointer<DigitorSdkWorkerHandle>);
typedef _CommandDart = int Function(Pointer<DigitorSdkWorkerHandle>);
typedef _DestroyNative = Void Function(Pointer<DigitorSdkWorkerHandle>);
typedef _DestroyDart = void Function(Pointer<DigitorSdkWorkerHandle>);

enum DigitorWorkerCompletion { completed, cancelled, failed }

final class DigitorWorkerProgress {
  const DigitorWorkerProgress(this.completedUnits, this.totalUnits);
  final int completedUnits;
  final int totalUnits;
}

final class DigitorSdkWorker {
  DigitorSdkWorker._(
    this._handle,
    this._start,
    this._cancel,
    this._destroy,
    this._progressCallback,
    this._completionCallback,
  );

  static DigitorSdkWorker create(
    DynamicLibrary library, {
    required bool exportJob,
    required int totalUnits,
  }) {
    if (totalUnits <= 0) {
      throw ArgumentError.value(totalUnits, 'totalUnits');
    }
    final progressController =
        StreamController<DigitorWorkerProgress>.broadcast();
    final completionController =
        StreamController<DigitorWorkerCompletion>.broadcast();
    final progress = NativeCallable<_ProgressNative>.listener((
      Pointer<Void> _,
      int completed,
      int total,
    ) {
      progressController.add(DigitorWorkerProgress(completed, total));
    });
    final completion = NativeCallable<_CompletionNative>.listener((
      Pointer<Void> _,
      int result,
    ) {
      if (result >= 0 && result < DigitorWorkerCompletion.values.length) {
        completionController.add(DigitorWorkerCompletion.values[result]);
      }
    });
    final create = library.lookupFunction<_CreateNative, _CreateDart>(
      'digitor_sdk_worker_create',
    );
    final handle = create(
      exportJob ? 1 : 0,
      totalUnits,
      progress.nativeFunction,
      completion.nativeFunction,
      nullptr,
    );
    if (handle == nullptr) {
      progress.close();
      completion.close();
      throw StateError('Native SDK worker creation failed');
    }
    return DigitorSdkWorker._(
        handle,
        library.lookupFunction<_CommandNative, _CommandDart>(
          'digitor_sdk_worker_start',
        ),
        library.lookupFunction<_CommandNative, _CommandDart>(
          'digitor_sdk_worker_cancel',
        ),
        library.lookupFunction<_DestroyNative, _DestroyDart>(
          'digitor_sdk_worker_destroy',
        ),
        progress,
        completion,
      )
      .._progressController = progressController
      .._completionController = completionController;
  }

  final Pointer<DigitorSdkWorkerHandle> _handle;
  final _CommandDart _start;
  final _CommandDart _cancel;
  final _DestroyDart _destroy;
  final NativeCallable<_ProgressNative> _progressCallback;
  final NativeCallable<_CompletionNative> _completionCallback;
  late final StreamController<DigitorWorkerProgress> _progressController;
  late final StreamController<DigitorWorkerCompletion> _completionController;
  bool _disposed = false;

  Stream<DigitorWorkerProgress> get progress => _progressController.stream;
  Stream<DigitorWorkerCompletion> get completion =>
      _completionController.stream;

  bool start() {
    _ensureAlive();
    return _start(_handle) != 0;
  }

  bool cancel() {
    _ensureAlive();
    return _cancel(_handle) != 0;
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _destroy(_handle);
    _progressCallback.close();
    _completionCallback.close();
    unawaited(_progressController.close());
    unawaited(_completionController.close());
  }

  void _ensureAlive() {
    if (_disposed) throw StateError('SDK worker is disposed');
  }
}
