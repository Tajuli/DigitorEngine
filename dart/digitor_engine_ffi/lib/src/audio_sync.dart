import 'dart:ffi';

import 'library_loader.dart';

final class _NativeAudioSyncSnapshot extends Struct {
  @Int32()
  external int backend;

  @Int32()
  external int measuredAvailable;

  @Int32()
  external int manualOverride;

  @Int64()
  external int measuredLatencyUs;

  @Int64()
  external int manualOffsetUs;

  @Int64()
  external int effectiveOffsetUs;
}

typedef _ProbeNative = _NativeAudioSyncSnapshot Function(Int64, Int32);
typedef _ProbeDart = _NativeAudioSyncSnapshot Function(int, int);
typedef _CompensateNative = Int64 Function(Int64, _NativeAudioSyncSnapshot);
typedef _CompensateDart = int Function(int, _NativeAudioSyncSnapshot);

final class DigitorAudioSyncSnapshot {
  const DigitorAudioSyncSnapshot({
    required this.backend,
    required this.measuredAvailable,
    required this.manualOverride,
    required this.measuredLatencyUs,
    required this.manualOffsetUs,
    required this.effectiveOffsetUs,
  });

  final int backend;
  final bool measuredAvailable;
  final bool manualOverride;
  final int measuredLatencyUs;
  final int manualOffsetUs;
  final int effectiveOffsetUs;
}

final class DigitorAudioSync {
  DigitorAudioSync({DynamicLibrary? library})
    : _library = library ?? DigitorLibraryLoader.open() {
    _probe = _library.lookupFunction<_ProbeNative, _ProbeDart>(
      'digitor_audio_sync_probe',
    );
    _compensate = _library.lookupFunction<_CompensateNative, _CompensateDart>(
      'digitor_audio_sync_compensate_clock',
    );
  }

  final DynamicLibrary _library;
  late final _ProbeDart _probe;
  late final _CompensateDart _compensate;

  DigitorAudioSyncSnapshot probe({
    int manualOffsetUs = 0,
    bool manualOverride = false,
  }) {
    final native = _probe(manualOffsetUs, manualOverride ? 1 : 0);
    return DigitorAudioSyncSnapshot(
      backend: native.backend,
      measuredAvailable: native.measuredAvailable != 0,
      manualOverride: native.manualOverride != 0,
      measuredLatencyUs: native.measuredLatencyUs,
      manualOffsetUs: native.manualOffsetUs,
      effectiveOffsetUs: native.effectiveOffsetUs,
    );
  }

  int compensateClock(int audioClockUs, DigitorAudioSyncSnapshot snapshot) {
    final native = _probe(
      snapshot.manualOffsetUs,
      snapshot.manualOverride ? 1 : 0,
    );
    return _compensate(audioClockUs, native);
  }
}
