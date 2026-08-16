import 'engine.dart';
import 'production_media.dart';

/// Immutable UI-facing description of the media currently owned by the
/// DigitorEngine production pipeline.
final class DigitorProductionMediaSnapshot {
  const DigitorProductionMediaSnapshot({
    required this.path,
    required this.decoder,
    required this.firstFrame,
    required this.duration,
    required this.nativeSurface,
    required this.strictGpuPath,
  });

  final String path;
  final DigitorProductionDecoderInfo decoder;
  final DigitorProductionDecodedFrameInfo firstFrame;

  /// Authoritative duration probed by the native media facade.
  /// May be zero only when the underlying platform cannot report duration.
  final Duration duration;

  final DigitorProductionNativeSurface? nativeSurface;

  /// True when this media facade was opened in explicit strict GPU mode.
  /// Strict mode requires zero-copy native decode and disables CPU-copy
  /// fallback for this source.
  final bool strictGpuPath;
}

/// Engine-owned production media facade used by Flutter applications.
///
/// Applications provide the selected renderer and media path. This facade is
/// tolerant by default because editor workspaces use it for auxiliary metadata
/// and first-frame probing. Callers that explicitly need a strict native-surface
/// probe can set [requireZeroCopy] to true. Registered production preview/export
/// bypasses this facade and keeps its own strict GPU policy.
final class DigitorProductionMediaPipeline {
  DigitorProductionMediaPipeline({
    required DigitorRendererInfo renderer,
    bool requireZeroCopy = false,
  }) : _renderer = renderer,
       _requireZeroCopy = requireZeroCopy;

  final DigitorRendererInfo _renderer;
  final bool _requireZeroCopy;
  DigitorProductionMediaSource? _source;
  DigitorProductionMediaSnapshot? _snapshot;
  bool _closed = false;

  DigitorProductionMediaSnapshot? get snapshot => _snapshot;
  bool get hasMedia => _source != null;

  DigitorProductionMediaSnapshot open(String path) {
    _ensureOpen();
    if (path.isEmpty) {
      throw ArgumentError.value(path, 'path', 'must not be empty');
    }

    final strictGpuPath = _renderer.isGpu && _requireZeroCopy;
    DigitorProductionMediaSource? next;
    try {
      next = DigitorProductionMediaSource.open(
        path,
        hardwareDecode: DigitorHardwareDecode.automatic,
        allowCpuFallback: !strictGpuPath,
        requireZeroCopy: strictGpuPath,
      );
      final decoder = next.decoderInfo;
      final duration = next.duration;
      final firstFrame = next.decode(0);

      DigitorProductionNativeSurface? surface;
      if (strictGpuPath) {
        if (!decoder.hardwareAccelerated ||
            !decoder.nativeSurfaceOutput ||
            !firstFrame.gpuResident ||
            firstFrame.cpuResident) {
          throw StateError(
            'Selected GPU backend requires hardware decode with a native '
            'zero-copy surface; CPU fallback is disabled.',
          );
        }
        surface = next.nativeSurface;
        if (surface.nativeHandle == 0) {
          throw StateError('Production decoder returned an empty GPU handle.');
        }
      } else if (firstFrame.gpuResident && decoder.nativeSurfaceOutput) {
        // An explicitly selected CPU renderer does not require a GPU surface,
        // but preserve one when the decoder happens to expose it.
        try {
          surface = next.nativeSurface;
        } catch (_) {
          surface = null;
        }
      }

      final value = DigitorProductionMediaSnapshot(
        path: path,
        decoder: decoder,
        firstFrame: firstFrame,
        duration: duration,
        nativeSurface: surface,
        strictGpuPath: strictGpuPath,
      );
      final previous = _source;
      _source = next;
      next = null;
      _snapshot = value;
      previous?.close();
      return value;
    } finally {
      next?.close();
    }
  }

  void seek(Duration position) {
    _ensureOpen();
    final source = _source;
    if (source == null) {
      throw StateError('Open media before seeking.');
    }
    source.seek(position);
  }

  DigitorProductionDecodedFrameInfo decode(int frameNumber) {
    _ensureOpen();
    final source = _source;
    if (source == null) {
      throw StateError('Open media before decoding.');
    }
    return source.decode(frameNumber);
  }

  DigitorProductionNativeSurface nativeSurface() {
    _ensureOpen();
    final source = _source;
    if (source == null) {
      throw StateError('Open media before requesting a surface.');
    }
    return source.nativeSurface;
  }

  void clear() {
    if (_closed) {
      return;
    }
    _source?.close();
    _source = null;
    _snapshot = null;
  }

  void close() {
    if (_closed) {
      return;
    }
    clear();
    _closed = true;
  }

  void _ensureOpen() {
    if (_closed) {
      throw StateError('DigitorProductionMediaPipeline is closed.');
    }
  }
}
