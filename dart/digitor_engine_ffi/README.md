# digitor_engine_ffi

Drop-in Flutter/Dart FFI integration for DigitorEngine.

The package uses Flutter/Dart native-assets build hooks to compile and bundle the native `digitor_engine` shared library with the consuming app. Consumer applications do not need to copy DLL/SO files or add DigitorEngine CMake targets manually.

## Requirements

- Flutter 3.38 or newer
- Dart 3.10 or newer
- CMake 3.21 or newer
- A normal Flutter toolchain for the target platform
- Android: Android SDK + NDK
- iOS/macOS: Xcode toolchain
- Windows: Visual Studio C++ toolchain

## Add to a Flutter app

Until the package is published separately, reference this repository and package path:

```yaml
dependencies:
  digitor_engine_ffi:
    git:
      url: https://github.com/Tajuli/DigitorEngine.git
      path: dart/digitor_engine_ffi
      ref: main
```

For development against a local checkout:

```yaml
dependencies:
  digitor_engine_ffi:
    path: ../DigitorEngine/dart/digitor_engine_ffi
```

Then run:

```bash
flutter pub get
```

The package build hook builds and bundles the native library for the app's target OS and architecture.

## Minimal usage

```dart
import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';

Future<void> useEngine() async {
  final engine = DigitorEngine.initialize();
  try {
    final renderer = engine.rendererInfo;
    print('DigitorEngine ${DigitorEngine.version}');
    print('${renderer.backendName}: ${renderer.deviceName}');

    final session = engine.createSession();
    try {
      session.setColor(
        const DigitorColorControls(
          exposure: 0.15,
          contrast: 1.05,
          saturation: 1.1,
        ),
      );

      await session.seek(0);

      final capabilities = session.previewCapabilities;
      print('Native GPU preview: ${capabilities.nativeGpuPreviewAvailable}');
    } finally {
      await session.dispose();
    }
  } finally {
    await engine.close();
  }
}
```

## Compatibility preview

`DigitorSession.preview()` exposes the existing CPU-readable compatibility preview as a Dart-owned RGBA byte buffer:

```dart
final frame = await session.preview(
  frame: 0,
  width: 1280,
  height: 720,
);

final Uint8List rgba = frame.rgba;
```

This compatibility path is intentionally **not** presented as a zero-copy native GPU texture. Check `session.previewCapabilities` before selecting a native-GPU presentation path. `DigitorPreviewMode.nativeGpuStrict` remains fail-closed when a production Flutter texture registrar/presenter is not bound by the native platform integration.

## Export

```dart
await session.export(
  path: outputPath,
  firstFrame: 0,
  lastFrame: 299,
  width: 1920,
  height: 1080,
  format: DigitorExportFormat.mp4,
  codec: DigitorVideoCodec.h264,
  onProgress: (progress) {
    print('${(progress.fraction * 100).toStringAsFixed(1)}%');
  },
);
```

Call `session.cancel()` to request cancellation of the active asynchronous session operation.

## Backend selection

The default is `DigitorBackend.automatic` with CPU fallback allowed. Applications can explicitly request a backend when required:

```dart
final engine = DigitorEngine.initialize(
  preferredBackend: DigitorBackend.vulkan,
  allowCpuFallback: false,
);
```

The engine's production policy remains GPU-first. Once a GPU backend has been selected, a later GPU execution failure must be surfaced instead of silently changing the active job to CPU.

## Lifetime rules

- Initialize the process-wide engine before creating sessions.
- Keep a session alive until its async operation completes or is cancelled.
- Do not start two async operations on the same session concurrently.
- Dispose sessions before closing the engine.
- `DigitorEngine.close()` also disposes sessions created by that instance.
- A compatibility preview copies native RGBA data before returning, so the returned `Uint8List` is independent of the next native preview request.

## Native production preview boundary

The native engine contains a production GPU-frame/runtime path, but the current generic FFI package does not claim that a Flutter texture registrar is automatically bound on every platform. The package exposes native preview capabilities rather than silently substituting a CPU pointer for a native GPU resource. Platform-specific zero-copy presentation can be added behind the same public capability contract without changing normal Flutter app initialization.
