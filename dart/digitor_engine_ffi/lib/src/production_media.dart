@DefaultAsset('package:digitor_engine_ffi/digitor_engine_ffi.dart')
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'engine.dart';

enum DigitorHardwareDecode {
  automatic(0),
  cpu(1),
  dxva(2),
  videoToolbox(3),
  mediaCodec(4);

  const DigitorHardwareDecode(this.nativeValue);

  final int nativeValue;

  static DigitorHardwareDecode fromNative(int value) => values.firstWhere(
    (item) => item.nativeValue == value,
    orElse: () => automatic,
  );
}

enum DigitorProductionMediaPixelFormat {
  rgba32Float(0),
  rgba8(1),
  bgra8(2),
  nv12(3),
  yuv420p(4),
  p010(5),
  yuv420p10(6);

  const DigitorProductionMediaPixelFormat(this.nativeValue);

  final int nativeValue;

  static DigitorProductionMediaPixelFormat fromNative(int value) =>
      values.firstWhere(
        (item) => item.nativeValue == value,
        orElse: () => rgba32Float,
      );
}

enum DigitorNativeMediaPlatform {
  none(0),
  windows(1),
  android(2),
  apple(3),
  vulkan(4);

  const DigitorNativeMediaPlatform(this.nativeValue);

  final int nativeValue;

  static DigitorNativeMediaPlatform fromNative(int value) => values.firstWhere(
    (item) => item.nativeValue == value,
    orElse: () => none,
  );
}

enum DigitorNativeMediaHandleType {
  none(0),
  d3d11Texture2d(1),
  d3d12Resource(2),
  dxgiSharedHandle(3),
  aHardwareBuffer(10),
  androidSurfaceTexture(11),
  cvPixelBuffer(20),
  ioSurface(21),
  metalTexture(22),
  vulkanImage(30),
  vulkanExternalMemory(31);

  const DigitorNativeMediaHandleType(this.nativeValue);

  final int nativeValue;

  static DigitorNativeMediaHandleType fromNative(int value) => values
      .firstWhere((item) => item.nativeValue == value, orElse: () => none);
}

enum DigitorNativeMediaPixelFormat {
  unknown(0),
  nv12(1),
  p010(2),
  yuv420p(3),
  yuv420p10(4),
  bgra8(5),
  rgba8(6),
  rgba16Float(7),
  rgba32Float(8);

  const DigitorNativeMediaPixelFormat(this.nativeValue);

  final int nativeValue;

  static DigitorNativeMediaPixelFormat fromNative(int value) => values
      .firstWhere((item) => item.nativeValue == value, orElse: () => unknown);
}

enum DigitorNativeMediaSyncType {
  none(0),
  d3d11Fence(1),
  d3d12Fence(2),
  metalSharedEvent(3),
  vulkanSemaphore(4),
  syncFd(5);

  const DigitorNativeMediaSyncType(this.nativeValue);

  final int nativeValue;

  static DigitorNativeMediaSyncType fromNative(int value) => values.firstWhere(
    (item) => item.nativeValue == value,
    orElse: () => none,
  );
}

final class DigitorProductionDecoderInfo {
  const DigitorProductionDecoderInfo({
    required this.selectedDecode,
    required this.hardwareAccelerated,
    required this.nativeSurfaceOutput,
    required this.nativeHandleType,
    required this.implementation,
  });

  final DigitorHardwareDecode selectedDecode;
  final bool hardwareAccelerated;
  final bool nativeSurfaceOutput;
  final DigitorNativeMediaHandleType nativeHandleType;
  final String implementation;
}

final class DigitorProductionDecodedFrameInfo {
  const DigitorProductionDecodedFrameInfo({
    required this.frameNumber,
    required this.pts,
    required this.duration,
    required this.width,
    required this.height,
    required this.pixelFormat,
    required this.gpuResident,
    required this.cpuResident,
  });

  final int frameNumber;
  final Duration pts;
  final Duration duration;
  final int width;
  final int height;
  final DigitorProductionMediaPixelFormat pixelFormat;
  final bool gpuResident;
  final bool cpuResident;
}

/// Borrowed descriptor for the most recently decoded native GPU surface.
///
/// The native handle stays retained by [DigitorProductionMediaSource] until the
/// next successful decode, seek, or close. It must not be released, mapped as
/// CPU memory, or retained independently by Dart code.
final class DigitorProductionNativeSurface {
  const DigitorProductionNativeSurface({
    required this.platform,
    required this.handleType,
    required this.pixelFormat,
    required this.width,
    required this.height,
    required this.planeCount,
    required this.arraySlice,
    required this.nativeHandle,
    required this.nativeDevice,
    required this.allocationSize,
    required this.timestamp,
    required this.acquireSyncType,
    required this.acquireSyncHandle,
    required this.acquireSyncValue,
    required this.colorPrimaries,
    required this.transferFunction,
    required this.matrixCoefficients,
    required this.fullRange,
    required this.chromaLocation,
  });

  final DigitorNativeMediaPlatform platform;
  final DigitorNativeMediaHandleType handleType;
  final DigitorNativeMediaPixelFormat pixelFormat;
  final int width;
  final int height;
  final int planeCount;
  final int arraySlice;
  final int nativeHandle;
  final int nativeDevice;
  final int allocationSize;
  final Duration timestamp;
  final DigitorNativeMediaSyncType acquireSyncType;
  final int acquireSyncHandle;
  final int acquireSyncValue;
  final int colorPrimaries;
  final int transferFunction;
  final int matrixCoefficients;
  final bool fullRange;
  final int chromaLocation;
}

/// Real media decoder owned by the native DigitorEngine package.
///
/// For production GPU preview/export, set [requireZeroCopy] to true. Explicit
/// hardware decode requests are strict in the native decoder; a selected GPU
/// path never silently converts a native surface to CPU pixels.
final class DigitorProductionMediaSource {
  DigitorProductionMediaSource._(this._handle);

  Pointer<_ProductionMediaSourceNative> _handle;
  bool _closed = false;

  static bool get ffmpegAvailable => _mediaFfmpegAvailable() != 0;

  static DigitorProductionMediaSource open(
    String path, {
    DigitorHardwareDecode hardwareDecode = DigitorHardwareDecode.automatic,
    bool allowCpuFallback = true,
    bool requireZeroCopy = false,
    int cacheCapacity = 16,
  }) {
    if (path.isEmpty) {
      throw ArgumentError.value(path, 'path', 'must not be empty');
    }
    if (cacheCapacity <= 0) {
      throw ArgumentError.value(
        cacheCapacity,
        'cacheCapacity',
        'must be positive',
      );
    }

    final nativePath = path.toNativeUtf8();
    final options = calloc<_ProductionMediaOptionsNative>();
    final out = calloc<Pointer<_ProductionMediaSourceNative>>();
    try {
      options.ref
        ..structSize = sizeOf<_ProductionMediaOptionsNative>()
        ..apiVersion = 1
        ..hardwareDecode = hardwareDecode.nativeValue
        ..allowCpuFallback = allowCpuFallback ? 1 : 0
        ..requireZeroCopy = requireZeroCopy ? 1 : 0
        ..reserved = 0
        ..cacheCapacity = cacheCapacity;
      final result = _mediaOpen(nativePath, options, out);
      _check('productionMediaOpen', result);
      if (out.value == nullptr) {
        throw const DigitorEngineException('productionMediaOpen', 100);
      }
      return DigitorProductionMediaSource._(out.value);
    } finally {
      calloc.free(out);
      calloc.free(options);
      malloc.free(nativePath);
    }
  }

  DigitorProductionDecoderInfo get decoderInfo {
    _ensureOpen();
    final out = calloc<_ProductionDecoderInfoNative>();
    try {
      out.ref
        ..structSize = sizeOf<_ProductionDecoderInfoNative>()
        ..apiVersion = 1;
      _check('productionMediaGetInfo', _mediaGetInfo(_handle, out));
      return DigitorProductionDecoderInfo(
        selectedDecode: DigitorHardwareDecode.fromNative(
          out.ref.selectedHardwareDecode,
        ),
        hardwareAccelerated: out.ref.hardwareAccelerated != 0,
        nativeSurfaceOutput: out.ref.nativeSurfaceOutput != 0,
        nativeHandleType: DigitorNativeMediaHandleType.fromNative(
          out.ref.nativeHandleType,
        ),
        implementation: _fixedCString(out.ref.implementation, 128),
      );
    } finally {
      calloc.free(out);
    }
  }

  /// Container/video-stream duration probed by the native media facade.
  /// A zero duration means the platform decoder could not expose duration.
  Duration get duration {
    _ensureOpen();
    final out = calloc<Int64>();
    try {
      _check('productionMediaGetDuration', _mediaGetDurationUs(_handle, out));
      return Duration(microseconds: out.value);
    } finally {
      calloc.free(out);
    }
  }

  void seek(Duration position) {
    _ensureOpen();
    if (position.isNegative) {
      throw ArgumentError.value(position, 'position', 'must not be negative');
    }
    _check('productionMediaSeek', _mediaSeek(_handle, position.inMicroseconds));
  }

  DigitorProductionDecodedFrameInfo decode(int frameNumber) {
    _ensureOpen();
    if (frameNumber < 0) {
      throw ArgumentError.value(
        frameNumber,
        'frameNumber',
        'must not be negative',
      );
    }
    final out = calloc<_ProductionDecodedFrameInfoNative>();
    try {
      out.ref
        ..structSize = sizeOf<_ProductionDecodedFrameInfoNative>()
        ..apiVersion = 1;
      _check('productionMediaDecode', _mediaDecode(_handle, frameNumber, out));
      return DigitorProductionDecodedFrameInfo(
        frameNumber: out.ref.frameNumber,
        pts: Duration(microseconds: out.ref.ptsUs),
        duration: Duration(microseconds: out.ref.durationUs),
        width: out.ref.width,
        height: out.ref.height,
        pixelFormat: DigitorProductionMediaPixelFormat.fromNative(
          out.ref.pixelFormat,
        ),
        gpuResident: out.ref.gpuResident != 0,
        cpuResident: out.ref.cpuResident != 0,
      );
    } finally {
      calloc.free(out);
    }
  }

  /// Returns the retained native decoder surface for the last decoded frame.
  ///
  /// Throws [DigitorEngineException] with backend-unavailable when the current
  /// frame has no native GPU surface (for example a CPU decode path).
  DigitorProductionNativeSurface get nativeSurface {
    _ensureOpen();
    final out = calloc<_ProductionNativeSurfaceNative>();
    try {
      out.ref
        ..structSize = sizeOf<_ProductionNativeSurfaceNative>()
        ..apiVersion = 1;
      _check(
        'productionMediaGetNativeSurface',
        _mediaGetNativeSurface(_handle, out),
      );
      return DigitorProductionNativeSurface(
        platform: DigitorNativeMediaPlatform.fromNative(out.ref.platform),
        handleType: DigitorNativeMediaHandleType.fromNative(out.ref.handleType),
        pixelFormat: DigitorNativeMediaPixelFormat.fromNative(
          out.ref.pixelFormat,
        ),
        width: out.ref.width,
        height: out.ref.height,
        planeCount: out.ref.planeCount,
        arraySlice: out.ref.arraySlice,
        nativeHandle: out.ref.nativeHandle,
        nativeDevice: out.ref.nativeDevice,
        allocationSize: out.ref.allocationSize,
        timestamp: Duration(microseconds: out.ref.timestampUs),
        acquireSyncType: DigitorNativeMediaSyncType.fromNative(
          out.ref.acquireSyncType,
        ),
        acquireSyncHandle: out.ref.acquireSyncHandle,
        acquireSyncValue: out.ref.acquireSyncValue,
        colorPrimaries: out.ref.colorPrimaries,
        transferFunction: out.ref.transferFunction,
        matrixCoefficients: out.ref.matrixCoefficients,
        fullRange: out.ref.fullRange != 0,
        chromaLocation: out.ref.chromaLocation,
      );
    } finally {
      calloc.free(out);
    }
  }

  void close() {
    if (_closed) {
      return;
    }
    _mediaClose(_handle);
    _handle = nullptr;
    _closed = true;
  }

  void _ensureOpen() {
    if (_closed || _handle == nullptr) {
      throw StateError('DigitorProductionMediaSource is closed');
    }
  }
}

void _check(String operation, int result) {
  if (result != 0) {
    throw DigitorEngineException(operation, result);
  }
}

String _fixedCString(Array<Int8> chars, int length) {
  final units = <int>[];
  for (var index = 0; index < length; index++) {
    final value = chars[index];
    if (value == 0) {
      break;
    }
    units.add(value & 0xff);
  }
  return String.fromCharCodes(units);
}

final class _ProductionMediaSourceNative extends Opaque {}

final class _ProductionMediaOptionsNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  @Uint32()
  external int hardwareDecode;

  @Uint8()
  external int allowCpuFallback;

  @Uint8()
  external int requireZeroCopy;

  @Uint16()
  external int reserved;

  @Uint32()
  external int cacheCapacity;
}

final class _ProductionDecoderInfoNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  @Uint32()
  external int selectedHardwareDecode;

  @Uint8()
  external int hardwareAccelerated;

  @Uint8()
  external int nativeSurfaceOutput;

  @Uint16()
  external int reserved;

  @Uint32()
  external int nativeHandleType;

  @Array(128)
  external Array<Int8> implementation;
}

final class _ProductionDecodedFrameInfoNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  @Int64()
  external int frameNumber;

  @Int64()
  external int ptsUs;

  @Int64()
  external int durationUs;

  @Uint32()
  external int width;

  @Uint32()
  external int height;

  @Uint32()
  external int pixelFormat;

  @Uint8()
  external int gpuResident;

  @Uint8()
  external int cpuResident;

  @Uint16()
  external int reserved;
}

final class _ProductionNativeSurfaceNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  @Uint32()
  external int platform;

  @Uint32()
  external int handleType;

  @Uint32()
  external int pixelFormat;

  @Uint32()
  external int width;

  @Uint32()
  external int height;

  @Uint32()
  external int planeCount;

  @Uint32()
  external int arraySlice;

  @Uint64()
  external int nativeHandle;

  @Uint64()
  external int nativeDevice;

  @Uint64()
  external int allocationSize;

  @Int64()
  external int timestampUs;

  @Uint32()
  external int acquireSyncType;

  @Uint32()
  external int reserved0;

  @Uint64()
  external int acquireSyncHandle;

  @Uint64()
  external int acquireSyncValue;

  @Int32()
  external int colorPrimaries;

  @Int32()
  external int transferFunction;

  @Int32()
  external int matrixCoefficients;

  @Uint8()
  external int fullRange;

  @Uint8()
  external int chromaLocation;

  @Uint16()
  external int reserved1;
}

@Native<
  Int32 Function(
    Pointer<Utf8>,
    Pointer<_ProductionMediaOptionsNative>,
    Pointer<Pointer<_ProductionMediaSourceNative>>,
  )
>(symbol: 'digitor_production_media_open')
external int _mediaOpen(
  Pointer<Utf8> path,
  Pointer<_ProductionMediaOptionsNative> options,
  Pointer<Pointer<_ProductionMediaSourceNative>> outSource,
);

@Native<
  Int32 Function(
    Pointer<_ProductionMediaSourceNative>,
    Pointer<_ProductionDecoderInfoNative>,
  )
>(symbol: 'digitor_production_media_get_info')
external int _mediaGetInfo(
  Pointer<_ProductionMediaSourceNative> source,
  Pointer<_ProductionDecoderInfoNative> outInfo,
);

@Native<Int32 Function(Pointer<_ProductionMediaSourceNative>, Pointer<Int64>)>(
  symbol: 'digitor_production_media_get_duration_us',
)
external int _mediaGetDurationUs(
  Pointer<_ProductionMediaSourceNative> source,
  Pointer<Int64> outDurationUs,
);

@Native<Int32 Function(Pointer<_ProductionMediaSourceNative>, Int64)>(
  symbol: 'digitor_production_media_seek',
)
external int _mediaSeek(
  Pointer<_ProductionMediaSourceNative> source,
  int ptsUs,
);

@Native<
  Int32 Function(
    Pointer<_ProductionMediaSourceNative>,
    Int64,
    Pointer<_ProductionDecodedFrameInfoNative>,
  )
>(symbol: 'digitor_production_media_decode')
external int _mediaDecode(
  Pointer<_ProductionMediaSourceNative> source,
  int frameNumber,
  Pointer<_ProductionDecodedFrameInfoNative> outFrame,
);

@Native<
  Int32 Function(
    Pointer<_ProductionMediaSourceNative>,
    Pointer<_ProductionNativeSurfaceNative>,
  )
>(symbol: 'digitor_production_media_get_native_surface')
external int _mediaGetNativeSurface(
  Pointer<_ProductionMediaSourceNative> source,
  Pointer<_ProductionNativeSurfaceNative> outSurface,
);

@Native<Void Function(Pointer<_ProductionMediaSourceNative>)>(
  symbol: 'digitor_production_media_close',
)
external void _mediaClose(Pointer<_ProductionMediaSourceNative> source);

@Native<Uint8 Function()>(symbol: 'digitor_production_media_ffmpeg_available')
external int _mediaFfmpegAvailable();
