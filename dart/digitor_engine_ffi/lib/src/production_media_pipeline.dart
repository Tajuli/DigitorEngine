import 'engine.dart';
import 'production_media.dart';

/// Immutable UI-facing description of the media currently owned by the
/// DigitorEngine production pipeline.
final class DigitorProductionMediaSnapshot {
  const DigitorProductionMediaSnapshot({
    required this.path,
    required this.decoder,
    required this.firstFrame,
    required this.nativeSurface,
    required this.strictGpuPath,
  });

  final String path;
  final DigitorProductionDecoderInfo decoder;
  final DigitorProductionDecodedFrameInfo firstFrame;
  final DigitorProductionNativeSurface? nativeSurface;

  /// True when the selected renderer is GPU-backed. In this mode decode is
  /// opened with zero-copy required and CPU fallback disabled so an already
  /// selected GPU backend can never silently switch processing to CPU.
  final bool strictGpuPath;
}

/// Engine-owned production media facade used by Flutter applications.
///
/// Applications provide only the selected renderer and media path. Decode
/// policy, frame probing and native-surface ownership stay inside the engine
/// SDK. This keeps app code at the UI/command boundary and prevents a Flutter
/// caller from accidentally enabling a CPU copy after GPU selection.
final class DigitorProductionMediaPipeline {
  DigitorProductionMediaPipeline({required DigitorRendererInfo renderer})
    : _renderer = renderer;

  final DigitorRendererInfo _renderer;
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

    final strictGpuPath = _renderer.isGpu;
    DigitorProductionMediaSource? next;
    try {
      next = DigitorProductionMediaSource.open(
        path,
        hardwareDecode: DigitorHardwareDecode.automatic,
        allowCpuFallback: !strictGpuPath,
        requireZeroCopy: strictGpuPath,
      );
      final decoder = next.decoderInfo;
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
