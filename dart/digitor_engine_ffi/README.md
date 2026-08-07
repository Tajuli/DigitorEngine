# DigitorEngine Flutter FFI

Private Flutter integration package for DigitorEngine. The package builds or links the native engine for Android, Windows, iOS, and macOS and exposes a small Dart API over the stable C ABI.

This package is intentionally not published to pub.dev (`publish_to: none`). Digitor should consume it from the private DigitorEngine checkout.

## Add to a Flutter app

For a sibling checkout:

```yaml
dependencies:
  digitor_engine_ffi:
    path: ../DigitorEngine/dart/digitor_engine_ffi
```

A private Git dependency can also target the package subdirectory when the build environment already has access to the private repository:

```yaml
dependencies:
  digitor_engine_ffi:
    git:
      url: git@github.com:Tajuli/DigitorEngine.git
      ref: main
      path: dart/digitor_engine_ffi
```

## Basic usage

```dart
import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';

final engine = DigitorEngine.open();

final renderer = engine.initialize();
print('${renderer.backendName}: ${renderer.deviceName}');

final graph = engine.createNodeGraph();
try {
  final endpoints = graph.endpoints();
  final node = graph.addSerialAfter(endpoints.input, name: 'Grade');
  graph.select(node);
  graph.addPrimaryWheels(DigitorEngine.identityPrimaryWheels);

  print(graph.recipeIdentity());
} finally {
  graph.dispose();
  engine.shutdown();
}
```

`DigitorEngine.identityPrimaryWheels` uses the native identity convention: Lift and Offset master values are `0`, while Gamma and Gain master values are `1`.

## Renderer policy

The default configuration asks DigitorEngine to select its normal GPU-first backend. CPU fallback is allowed during initial backend selection unless explicitly disabled. Once a GPU backend is selected, application code should treat runtime GPU failures as failures rather than silently changing the rendering path.

An application can request a specific backend:

```dart
engine.initialize(
  configuration: const DigitorEngineConfiguration(
    preferredBackend: DigitorRendererBackend.vulkan,
    allowCpuFallback: false,
  ),
);
```

## Node graph operations

`DigitorNativeNodeGraph` exposes the production native node graph, including serial and parallel topology, node enable/bypass state, Primary Wheels, Log Wheels, RGB Curves, HSL Qualifier, 1D/3D LUT operations, built-in effects, and power windows.

The Dart package is an adapter only. Rendering math and graph execution remain in the native DigitorEngine runtime so Flutter UI code does not implement a second color pipeline.

## Lifetime

1. Open one `DigitorEngine` facade for the application process.
2. Call `initialize()` before creating engine resources.
3. Dispose node graphs, sessions, jobs, and presenter resources before shutdown.
4. Call `shutdown()` when the application is finished with the native engine.

The high-level `DigitorEngine` facade is the recommended entry point. Lower-level FFI adapters remain available for Digitor-specific integration code that needs them.
