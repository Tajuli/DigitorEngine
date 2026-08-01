import 'dart:ffi';

import 'library_loader.dart';

final class _NativeSync extends Opaque {}

final class DigitorPlaybackSyncSnapshot {
  const DigitorPlaybackSyncSnapshot({
    required this.effectiveOffsetUs,
    required this.probeGeneration,
    required this.lastProbeTimeUs,
    required this.measuredAvailable,
    required this.deviceChangePending,
  });

  final int effectiveOffsetUs;
  final int probeGeneration;
  final int lastProbeTimeUs;
  final bool measuredAvailable;
  final bool deviceChangePending;
}

final class DigitorLivePlaybackSync {
  DigitorLivePlaybackSync({
    int manualOffsetUs = 0,
    bool manualOverride = false,
    DynamicLibrary? library,
  }) : _library = library ?? DigitorLibraryLoader.open() {
    _create = _library.lookupFunction<
        Pointer<_NativeSync> Function(Int64, Int32),
        Pointer<_NativeSync> Function(
            int, int)>('digitor_live_playback_sync_create');
    _destroy = _library.lookupFunction<Void Function(Pointer<_NativeSync>),
        void Function(Pointer<_NativeSync>)>(
      'digitor_live_playback_sync_destroy',
    );
    _notify = _library.lookupFunction<Void Function(Pointer<_NativeSync>),
        void Function(Pointer<_NativeSync>)>(
      'digitor_live_playback_sync_notify_device_change',
    );
    _refresh = _library.lookupFunction<
        Int32 Function(Pointer<_NativeSync>, Int64),
        int Function(Pointer<_NativeSync>, int)>(
      'digitor_live_playback_sync_refresh',
    );
    _clock = _library.lookupFunction<
        Int64 Function(Pointer<_NativeSync>, Int64),
        int Function(Pointer<_NativeSync>, int)>(
      'digitor_live_playback_sync_clock',
    );
    _handle = _create(manualOffsetUs, manualOverride ? 1 : 0);
    if (_handle == nullptr) {
      throw StateError('Failed to create live playback sync controller.');
    }
  }

  final DynamicLibrary _library;
  late final Pointer<_NativeSync> Function(int, int) _create;
  late final void Function(Pointer<_NativeSync>) _destroy;
  late final void Function(Pointer<_NativeSync>) _notify;
  late final int Function(Pointer<_NativeSync>, int) _refresh;
  late final int Function(Pointer<_NativeSync>, int) _clock;
  late Pointer<_NativeSync> _handle;

  void notifyDeviceChanged() => _notify(_handle);

  bool refresh(int nowUs) => _refresh(_handle, nowUs) != 0;

  int compensatedClockUs(int rawAudioClockUs) =>
      _clock(_handle, rawAudioClockUs);

  void dispose() {
    if (_handle != nullptr) {
      _destroy(_handle);
      _handle = nullptr;
    }
  }
}
