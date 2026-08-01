import 'dart:async';
import 'dart:ffi';

import 'native_api.dart';

final class DigitorEngineException implements Exception {
  const DigitorEngineException(this.operation, this.result);
  final String operation;
  final int result;
  @override String toString() => 'DigitorEngineException($operation, result: $result)';
}

final class DigitorTimelineAudioSession {
  DigitorTimelineAudioSession._(this._api, this._handle);

  static Future<DigitorTimelineAudioSession> create({
    required DigitorSessionConfig config,
    DigitorNativeApi? api,
  }) async {
    final nativeApi = api ?? FfiDigitorNativeApi.open();
    final call = nativeApi.create(config);
    if (call.result != 0 || call.value == null) {
      throw DigitorEngineException('create', call.result);
    }
    return DigitorTimelineAudioSession._(nativeApi, call.value!);
  }

  final DigitorNativeApi _api;
  Pointer<DigitorNativeSession>? _handle;
  int _lastPublishedRevision = 0;
  final StreamController<DigitorSessionStatus> _statusController =
      StreamController<DigitorSessionStatus>.broadcast(sync: true);

  Stream<DigitorSessionStatus> get statusStream => _statusController.stream;
  bool get isDisposed => _handle == null;

  Future<void> publishRevision(int revision) async {
    if (revision <= _lastPublishedRevision) {
      throw ArgumentError.value(revision, 'revision', 'must increase monotonically');
    }
    _check('publish', _api.publish(_requireHandle(), revision));
    _lastPublishedRevision = revision;
    await refreshStatus();
  }

  Future<void> play() async { _check('play', _api.play(_requireHandle())); await refreshStatus(); }
  Future<void> pause() async { _check('pause', _api.pause(_requireHandle())); await refreshStatus(); }
  Future<void> stop() async { _check('stop', _api.stop(_requireHandle())); await refreshStatus(); }
  Future<void> seek(int frame) async { _check('seek', _api.seek(_requireHandle(), frame)); await refreshStatus(); }

  Future<void> setAudioControls(DigitorAudioControls controls) async {
    _check('setAudioControls', _api.setAudioControls(_requireHandle(), controls));
  }

  Future<DigitorSessionStatus> refreshStatus() async {
    final call = _api.getStatus(_requireHandle());
    if (call.result != 0 || call.value == null) {
      throw DigitorEngineException('getStatus', call.result);
    }
    _statusController.add(call.value!);
    return call.value!;
  }

  Future<DigitorSessionTelemetry> telemetry() async {
    final call = _api.getTelemetry(_requireHandle());
    if (call.result != 0 || call.value == null) {
      throw DigitorEngineException('getTelemetry', call.result);
    }
    return call.value!;
  }

  Future<void> dispose() async {
    final handle = _handle;
    if (handle == null) return;
    _handle = null;
    final result = _api.destroy(handle);
    await _statusController.close();
    if (result != 0) throw DigitorEngineException('destroy', result);
  }

  Pointer<DigitorNativeSession> _requireHandle() {
    final handle = _handle;
    if (handle == null) throw StateError('Digitor session is disposed');
    return handle;
  }

  void _check(String operation, int result) {
    if (result != 0) throw DigitorEngineException(operation, result);
  }
}
