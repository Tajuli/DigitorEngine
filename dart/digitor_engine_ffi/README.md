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

## Production node graph

`DigitorNodeGraph` is the Flutter-facing owner of the engine's real production node graph. It exposes serial/parallel topology plus Primary Wheels, Log Wheels, RGB Curves, HSL Qualifier, Correction, LUT 1D/3D, effects and Power Windows. It does not implement a second Dart color pipeline.

```dart
final graph = DigitorNodeGraph.create();
final nodes = graph.endpoints;
final grade = graph.addSerialAfter(nodes.input, name: 'Grade 01');
graph.select(grade);

graph.addCorrection(
  const DigitorCorrection(
    exposure: 0.15,
    contrast: 0.08,
    saturation: 0.1,
    temperature: 0.03,
  ),
);

graph.addPrimaryWheels(
  const DigitorPrimaryWheels(
    lift: DigitorPrimaryWheel(master: -0.02),
    gamma: DigitorPrimaryWheel(master: 0.03),
    gain: DigitorPrimaryWheel(master: 0.05),
  ),
);

print(graph.recipeIdentity);
print(graph.json);
```

Every render-affecting mutation increments `graphRevision`; parameter mutations also increment `parameterRevision`. Production preview/export sessions bind both revisions so the host sees the same immutable recipe identity in both paths.

## Production GPU Flutter bridge

`DigitorProductionSession` is separate from the legacy compatibility SDK. It never returns a CPU preview buffer. A platform Flutter embedding layer supplies a `DigitorProductionHost` containing native function pointers for media open/decode/import, GPU render, Flutter texture presentation/capabilities, export, cancellation and resource release.

```dart
final production = DigitorProductionSession.open(
  host: platformHost,
  mediaPath: inputPath,
  nodeGraph: graph,
);

final caps = production.previewCapabilities;
if (!caps.nativeGpuPreviewAvailable || caps.cpuFallbackOnly) {
  throw StateError(caps.reasonUnavailable);
}

final texture = production.preview(
  timestampUs: 1_000_000,
  width: 1920,
  height: 1080,
);

// The platform texture registrar presents texture.nativeHandle using the
// backend/handle/synchronization metadata in the descriptor. After Flutter's
// raster consumer releases this generation:
production.previewConsumed(texture.generation);

// After changing graph parameters, refresh the exact revision before the next
// preview or export.
graph.addEffect(
  const DigitorNodeEffect(
    type: DigitorNodeEffectType.vignette,
    amount: 0.15,
  ),
);
production.bindNodeGraph(graph);
```

The production C ABI validates that returned preview descriptors are real GPU resources, are ready, match the requested dimensions/timestamp and (when configured) match the selected device/context identity. CPU pointers and CPU fallback descriptors are rejected. A preview texture remains owned by the session until `previewConsumed()`.

The native engine also has a GPU-resident `ProcessedGpuFrame -> ProductionNodeGraph -> ProcessedGpuFrame` executor. A production decoder/importer can therefore feed an already imported hardware-decoded frame directly through the existing node graph without a source re-upload or validation readback.

`DigitorProductionHost` is intentionally an embedding boundary: Flutter texture registrars and decoder surface importers are platform APIs and must be implemented by the Windows/Android/iOS/macOS host layer. The shared FFI contract does not fabricate native handles or silently substitute CPU pixels when that host is missing.

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

With FFmpeg enabled on the compatibility session:

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

The production session's export callback is owned by its platform host and receives the exact graph and parameter revisions bound to the session. If that host uses DigitorEngine's FFmpeg export path, the same target-compatible FFmpeg SDK requirements apply.

Without FFmpeg, the engine core, renderer selection, SDK session, color controls, node graph and compatibility preview remain available; the FFmpeg-backed real media export path is unavailable by design.

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
- Keep a session alive until its operation completes or is cancelled.
- Dispose production sessions before disposing a bound `DigitorNodeGraph`.
- Each `DigitorProductionSession` is pinned to one `DigitorNodeGraph`; create a new production session to switch to a different graph.
- A production preview generation must be acknowledged with `previewConsumed()` before the graph is rebound or another preview/export begins.
- Do not mutate/rebind a production graph while a production operation is active.
- Dispose compatibility sessions before closing the engine.
- `DigitorEngine.close()` also disposes compatibility sessions created by that instance.
- A compatibility preview copies native RGBA data before returning, so the returned `Uint8List` is independent of the next native preview request.

## Native production preview boundary

The shared package now exposes the production node graph, a strict native-GPU texture descriptor, revision-pinned preview/export orchestration and the platform host ABI. Real Flutter `Texture` registration itself remains platform embedding work: Windows, Android, iOS and macOS hosts must connect their registrar and the selected renderer's real device/context. Missing or incompatible native presentation fails closed instead of becoming a CPU preview under a GPU label.
