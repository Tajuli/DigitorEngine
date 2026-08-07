import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'library_loader.dart';

enum DigitorRendererBackend {
  auto(0),
  vulkan(1),
  metal(2),
  d3d12(3),
  openGlEs(4),
  cpu(100);

  const DigitorRendererBackend(this.nativeValue);
  final int nativeValue;

  static DigitorRendererBackend fromNative(int value) => values.firstWhere(
        (backend) => backend.nativeValue == value,
        orElse: () => DigitorRendererBackend.auto,
      );
}

final class DigitorEngineConfiguration {
  const DigitorEngineConfiguration({
    this.preferredBackend = DigitorRendererBackend.auto,
    this.enableValidation = false,
    this.allowCpuFallback = true,
  });

  final DigitorRendererBackend preferredBackend;
  final bool enableValidation;
  final bool allowCpuFallback;
}

final class DigitorRendererInformation {
  const DigitorRendererInformation({
    required this.backend,
    required this.backendName,
    required this.deviceName,
    required this.isGpu,
    required this.supportsCompute,
    required this.supportsFp16,
    required this.supportsFp32,
  });

  final DigitorRendererBackend backend;
  final String backendName;
  final String deviceName;
  final bool isGpu;
  final bool supportsCompute;
  final bool supportsFp16;
  final bool supportsFp32;
}

final class DigitorEngineException implements Exception {
  const DigitorEngineException(this.operation, this.resultCode);

  final String operation;
  final int resultCode;

  @override
  String toString() => 'DigitorEngineException($operation, result=$resultCode)';
}

final class _DigitorEngineConfigNative extends Struct {
  @Int32()
  external int preferredBackend;

  @Uint8()
  external int enableValidation;

  @Uint8()
  external int allowCpuFallback;
}

final class _DigitorRendererInfoNative extends Struct {
  @Int32()
  external int backend;

  @Array(64)
  external Array<Uint8> backendName;

  @Array(128)
  external Array<Uint8> deviceName;

  @Uint8()
  external int isGpu;

  @Uint8()
  external int supportsCompute;

  @Uint8()
  external int supportsFp16;

  @Uint8()
  external int supportsFp32;
}

typedef _VersionNative = Pointer<Utf8> Function();
typedef _VersionDart = Pointer<Utf8> Function();
typedef _InitializeNative = Int32 Function(Pointer<_DigitorEngineConfigNative>);
typedef _InitializeDart = int Function(Pointer<_DigitorEngineConfigNative>);
typedef _ShutdownNative = Int32 Function();
typedef _ShutdownDart = int Function();
typedef _RendererInfoNative = Int32 Function(Pointer<_DigitorRendererInfoNative>);
typedef _RendererInfoDart = int Function(Pointer<_DigitorRendererInfoNative>);

/// Process-wide DigitorEngine lifecycle adapter.
///
/// DigitorEngine itself owns the renderer/backend singleton. This class keeps
/// Flutter callers on that same lifecycle instead of creating parallel media
/// engines in Dart. Once a GPU backend has been selected, runtime work is
/// expected to fail closed rather than silently changing to CPU.
final class DigitorNativeEngine {
  DigitorNativeEngine._(DynamicLibrary library)
      : _version = library.lookupFunction<_VersionNative, _VersionDart>(
          'digitor_get_version',
        ),
        _initialize = library.lookupFunction<_InitializeNative, _InitializeDart>(
          'digitor_initialize',
        ),
        _shutdown = library.lookupFunction<_ShutdownNative, _ShutdownDart>(
          'digitor_shutdown',
        ),
        _rendererInfo =
            library.lookupFunction<_RendererInfoNative, _RendererInfoDart>(
          'digitor_get_renderer_info',
        );

  factory DigitorNativeEngine.open({String? libraryPath}) => DigitorNativeEngine._(
        DigitorLibraryLoader.open(overridePath: libraryPath),
      );

  final _VersionDart _version;
  final _InitializeDart _initialize;
  final _ShutdownDart _shutdown;
  final _RendererInfoDart _rendererInfo;
  bool _initialized = false;

  bool get isInitialized => _initialized;
  String get version => _version().toDartString();

  DigitorRendererInformation initialize({
    DigitorEngineConfiguration configuration = const DigitorEngineConfiguration(),
  }) {
    if (_initialized) return rendererInformation();

    final config = calloc<_DigitorEngineConfigNative>();
    try {
      config.ref
        ..preferredBackend = configuration.preferredBackend.nativeValue
        ..enableValidation = configuration.enableValidation ? 1 : 0
        ..allowCpuFallback = configuration.allowCpuFallback ? 1 : 0;
      _check('initialize', _initialize(config));
      _initialized = true;
      return rendererInformation();
    } finally {
      calloc.free(config);
    }
  }

  DigitorRendererInformation rendererInformation() {
    final info = calloc<_DigitorRendererInfoNative>();
    try {
      _check('getRendererInfo', _rendererInfo(info));
      final value = info.ref;
      return DigitorRendererInformation(
        backend: DigitorRendererBackend.fromNative(value.backend),
        backendName: _nativeString(value.backendName, 64),
        deviceName: _nativeString(value.deviceName, 128),
        isGpu: value.isGpu != 0,
        supportsCompute: value.supportsCompute != 0,
        supportsFp16: value.supportsFp16 != 0,
        supportsFp32: value.supportsFp32 != 0,
      );
    } finally {
      calloc.free(info);
    }
  }

  void shutdown() {
    if (!_initialized) return;
    _check('shutdown', _shutdown());
    _initialized = false;
  }

  static String _nativeString(Array<Uint8> data, int length) {
    final bytes = <int>[];
    for (var index = 0; index < length; index++) {
      final value = data[index];
      if (value == 0) break;
      bytes.add(value);
    }
    return String.fromCharCodes(bytes);
  }

  static void _check(String operation, int result) {
    if (result != 0) throw DigitorEngineException(operation, result);
  }
}
