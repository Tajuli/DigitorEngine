import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'library_loader.dart';

enum DigitorExportJobState { idle, running, paused, completed, cancelled, failed }

final class DigitorExportJobConfig {
  const DigitorExportJobConfig({
    required this.inputPath,
    required this.outputPath,
    this.ffmpegPath = 'ffmpeg',
    this.checkpointPath = '',
    this.codec = 0,
    this.width = 1920,
    this.height = 1080,
    this.fpsNumerator = 30,
    this.fpsDenominator = 1,
    this.videoBitrate = 12000000,
    this.audioSampleRate = 48000,
    this.audioChannels = 2,
    this.preferHardware = true,
    this.allowSoftwareFallback = true,
    this.durationUs = 0,
  });

  final String inputPath;
  final String outputPath;
  final String ffmpegPath;
  final String checkpointPath;
  final int codec;
  final int width;
  final int height;
  final int fpsNumerator;
  final int fpsDenominator;
  final int videoBitrate;
  final int audioSampleRate;
  final int audioChannels;
  final bool preferHardware;
  final bool allowSoftwareFallback;
  final int durationUs;
}

final class DigitorExportJobSnapshot {
  const DigitorExportJobSnapshot({
    required this.state,
    required this.requestedBackend,
    required this.executedBackend,
    required this.usedFallback,
    required this.processExitCode,
    required this.durationUs,
    required this.completedUs,
    required this.progress,
    required this.generation,
    required this.diagnostic,
  });

  final DigitorExportJobState state;
  final int requestedBackend;
  final int executedBackend;
  final bool usedFallback;
  final int processExitCode;
  final int durationUs;
  final int completedUs;
  final double progress;
  final int generation;
  final String diagnostic;

  bool get isTerminal =>
      state == DigitorExportJobState.completed ||
      state == DigitorExportJobState.cancelled ||
      state == DigitorExportJobState.failed;
}

final class _NativeConfig extends Struct {
  external Pointer<Utf8> ffmpegPath;
  external Pointer<Utf8> inputPath;
  external Pointer<Utf8> outputPath;
  external Pointer<Utf8> checkpointPath;
  @Int32()
  external int codec;
  @Int32()
  external int width;
  @Int32()
  external int height;
  @Int32()
  external int fpsNumerator;
  @Int32()
  external int fpsDenominator;
  @Int64()
  external int videoBitrate;
  @Int32()
  external int audioSampleRate;
  @Int32()
  external int audioChannels;
  @Int32()
  external int preferHardware;
  @Int32()
  external int allowSoftwareFallback;
  @Int64()
  external int durationUs;
}

final class _NativeSnapshot extends Struct {
  @Int32()
  external int state;
  @Int32()
  external int requestedBackend;
  @Int32()
  external int executedBackend;
  @Int32()
  external int usedFallback;
  @Int32()
  external int processExitCode;
  @Int64()
  external int durationUs;
  @Int64()
  external int completedUs;
  @Double()
  external double progress;
  @Uint64()
  external int generation;
  @Array(256)
  external Array<Uint8> diagnostic;
}

typedef _CreateNative = Pointer<Void> Function(Pointer<_NativeConfig>);
typedef _CreateDart = Pointer<Void> Function(Pointer<_NativeConfig>);
typedef _ActionNative = Int32 Function(Pointer<Void>);
typedef _ActionDart = int Function(Pointer<Void>);
typedef _SnapshotNative = Int32 Function(Pointer<Void>, Pointer<_NativeSnapshot>);
typedef _SnapshotDart = int Function(Pointer<Void>, Pointer<_NativeSnapshot>);
typedef _DestroyNative = Void Function(Pointer<Void>);
typedef _DestroyDart = void Function(Pointer<Void>);

final class DigitorExportJob {
  DigitorExportJob(
    DigitorExportJobConfig config, {
    DynamicLibrary? library,
    String? libraryPath,
  }) : _library = library ?? DigitorLibraryLoader.open(overridePath: libraryPath) {
    _bind();
    final native = calloc<_NativeConfig>();
    final strings = <Pointer<Utf8>>[
      config.ffmpegPath.toNativeUtf8(),
      config.inputPath.toNativeUtf8(),
      config.outputPath.toNativeUtf8(),
      config.checkpointPath.toNativeUtf8(),
    ];
    try {
      native.ref
        ..ffmpegPath = strings[0]
        ..inputPath = strings[1]
        ..outputPath = strings[2]
        ..checkpointPath = strings[3]
        ..codec = config.codec
        ..width = config.width
        ..height = config.height
        ..fpsNumerator = config.fpsNumerator
        ..fpsDenominator = config.fpsDenominator
        ..videoBitrate = config.videoBitrate
        ..audioSampleRate = config.audioSampleRate
        ..audioChannels = config.audioChannels
        ..preferHardware = config.preferHardware ? 1 : 0
        ..allowSoftwareFallback = config.allowSoftwareFallback ? 1 : 0
        ..durationUs = config.durationUs;
      _handle = _create(native);
      if (_handle == nullptr) {
        throw StateError('DigitorEngine rejected the export job configuration.');
      }
    } finally {
      calloc.free(native);
      for (final value in strings) {
        calloc.free(value);
      }
    }
  }

  final DynamicLibrary _library;
  late final _CreateDart _create;
  late final _ActionDart _start;
  late final _ActionDart _pause;
  late final _ActionDart _resume;
  late final _ActionDart _cancel;
  late final _ActionDart _wait;
  late final _SnapshotDart _snapshot;
  late final _DestroyDart _destroy;
  Pointer<Void> _handle = nullptr;

  void _bind() {
    _create = _library.lookupFunction<_CreateNative, _CreateDart>('digitor_export_job_create');
    _start = _library.lookupFunction<_ActionNative, _ActionDart>('digitor_export_job_start');
    _pause = _library.lookupFunction<_ActionNative, _ActionDart>('digitor_export_job_pause');
    _resume = _library.lookupFunction<_ActionNative, _ActionDart>('digitor_export_job_resume');
    _cancel = _library.lookupFunction<_ActionNative, _ActionDart>('digitor_export_job_cancel');
    _wait = _library.lookupFunction<_ActionNative, _ActionDart>('digitor_export_job_wait');
    _snapshot = _library.lookupFunction<_SnapshotNative, _SnapshotDart>('digitor_export_job_snapshot');
    _destroy = _library.lookupFunction<_DestroyNative, _DestroyDart>('digitor_export_job_destroy');
  }

  bool start() => _invoke(_start);
  bool pause() => _invoke(_pause);
  bool resume() => _invoke(_resume);
  bool cancel() => _invoke(_cancel);
  bool wait() => _invoke(_wait);

  bool _invoke(_ActionDart action) {
    _ensureOpen();
    return action(_handle) != 0;
  }

  DigitorExportJobSnapshot snapshot() {
    _ensureOpen();
    final native = calloc<_NativeSnapshot>();
    try {
      if (_snapshot(_handle, native) == 0) {
        throw StateError('Failed to read the export job snapshot.');
      }
      final stateIndex = native.ref.state.clamp(0, DigitorExportJobState.values.length - 1);
      final bytes = <int>[];
      for (var i = 0; i < 256; i++) {
        final value = native.ref.diagnostic[i];
        if (value == 0) break;
        bytes.add(value);
      }
      return DigitorExportJobSnapshot(
        state: DigitorExportJobState.values[stateIndex],
        requestedBackend: native.ref.requestedBackend,
        executedBackend: native.ref.executedBackend,
        usedFallback: native.ref.usedFallback != 0,
        processExitCode: native.ref.processExitCode,
        durationUs: native.ref.durationUs,
        completedUs: native.ref.completedUs,
        progress: native.ref.progress,
        generation: native.ref.generation,
        diagnostic: String.fromCharCodes(bytes),
      );
    } finally {
      calloc.free(native);
    }
  }

  Stream<DigitorExportJobSnapshot> snapshots({
    Duration interval = const Duration(milliseconds: 200),
  }) async* {
    while (_handle != nullptr) {
      final value = snapshot();
      yield value;
      if (value.isTerminal) return;
      await Future<void>.delayed(interval);
    }
  }

  void dispose() {
    if (_handle == nullptr) return;
    _destroy(_handle);
    _handle = nullptr;
  }

  void _ensureOpen() {
    if (_handle == nullptr) throw StateError('Export job has been disposed.');
  }
}
