# Apple production-native provider

This package supplies the macOS and iOS implementation boundary for the production native-provider release gate.

## Repository-owned contract

- Metal-only timeline/device identity enforcement;
- Flutter macOS/iOS texture bridge requirement;
- IOSurface-backed CVPixelBuffer presentation and encode pool requirements;
- VideoToolbox hardware encoder binding;
- Metal completion synchronization;
- color/HDR attachment propagation;
- zero CPU readback and zero CPU staging enforcement;
- distinct macOS and iOS provider identities.

## Required Flutter plugin source

The engine does not vendor Flutter macOS or iOS embedding sources. A Flutter plugin target must implement `AppleFlutterTextureBridge` using the real Flutter texture registrar and return IOSurface/CVPixelBuffer-backed textures while retaining the exact `ProcessedGpuFramePtr` until Flutter releases the generation.

The bridge must not map Metal textures, create CPU RGBA buffers, or copy frames through Dart.

## Release configure

```bash
cmake --preset macos-native-release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/AppleNativeProvider.cmake" \
  -DDIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE=/path/to/digitor_macos_texture_bridge.mm \
  -DDIGITOR_APPLE_PROVIDER_IDENTITY=digitor-macos-provider-v1
```

For iOS, use the iOS toolchain/sysroot and an iOS Flutter bridge source with a distinct provider identity.

## Physical qualification

A real Mac and real iPhone must prove Metal preview presentation, VideoToolbox H.264/HEVC output, supported ProRes where available, HDR/alpha metadata, lifecycle and memory-pressure handling, output decode-and-compare, and zero-readback/staging telemetry. Simulator or compile-only evidence is not production qualification.
