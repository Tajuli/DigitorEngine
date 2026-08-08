# digitor_engine_ffi

Drop-in Flutter/Dart FFI integration for DigitorEngine.

The package uses Dart/Flutter native-assets build hooks to compile and bundle the native `digitor_engine` shared library with the consuming app. Consumer applications do not need to copy the DigitorEngine DLL/SO/dylib or manually add the DigitorEngine CMake target.

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

`flutter run`, `flutter build`, and `flutter test` invoke the package build hook automatically. The hook builds the engine for the app's actual target OS, architecture, Android API, or Apple SDK and bundles the resulting native library.

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

`DigitorEngine.initialize()` is idempotent inside one Dart isolate. Sessions created by the facade are tracked and are disposed automatically by `engine.close()` if the app has not already disposed them.

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

## FFmpeg media/export support

The base package deliberately has no hidden system dependency. It builds DigitorEngine without FFmpeg unless the root Flutter app explicitly supplies an FFmpeg development SDK.

To enable the engine's FFmpeg-backed real export path, configure the root app `pubspec.yaml`:

```yaml
hooks:
  user_defines:
    digitor_engine_ffi:
      ffmpeg_root: C:/DigitorSDK/ffmpeg
```

Relative paths are resolved relative to the root `pubspec.yaml`. When `ffmpeg_root` is supplied, the build fails immediately if the required FFmpeg headers/libraries are not present instead of silently producing an export-disabled engine. Use an FFmpeg SDK built for the same target OS and architecture. If that SDK uses shared FFmpeg libraries rather than static libraries, those runtime dependencies must also be packaged by the application/platform distribution.

With FFmpeg enabled:

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

Without FFmpeg, the engine core, renderer selection, SDK session, color controls, and compatibility preview remain available; real FFmpeg export is unavailable by design. Call `session.cancel()` to request cancellation of the active asynchronous session operation.

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
- Mutation methods reject calls while a session operation is active.
- Dispose sessions before closing the engine.
- `DigitorEngine.close()` also disposes sessions created by that instance.
- A compatibility preview copies native RGBA data before returning, so the returned `Uint8List` is independent of the next native preview request.

## Native production preview boundary

The native engine contains a production GPU-frame/runtime path, but the current generic FFI package does not claim that a Flutter texture registrar is automatically bound on every platform. The package exposes native preview capabilities rather than silently substituting a CPU pointer for a native GPU resource. Platform-specific zero-copy presentation can be added behind the same public capability contract without changing normal Flutter app initialization.
