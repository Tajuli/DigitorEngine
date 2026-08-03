# Apple production-native provider

This package provides the macOS and iOS implementation boundary for the production native-provider release gate.

## Repository-owned contract

- Metal-only timeline/device identity enforcement;
- IOSurface-backed CVPixelBuffer pool requirement;
- Flutter macOS/iOS texture bridge requirement;
- VideoToolbox hardware encoder binding;
- Metal completion synchronization;
- color, HDR and alpha attachment propagation;
- zero CPU readback and zero CPU staging enforcement;
- separate macOS and iOS provider identities in the strict registry.

## Required Flutter plugin source

The engine does not vendor Flutter Apple embedding sources. A Flutter plugin target must implement `AppleFlutterTextureBridge` using the real macOS or iOS texture registrar and retain the exact GPU-owned frame/CVPixelBuffer until Flutter releases the generation.

The bridge must not copy through CPU RGBA buffers, map Metal textures, or serialize pixels through Dart.

## Release configure

```bash
cmake --preset macos-native-release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/AppleNativeProvider.cmake" \
  -DDIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE=/path/to/digitor_macos_texture_bridge.mm \
  -DDIGITOR_APPLE_PROVIDER_IDENTITY=digitor-macos-provider-v1
```

For iOS, use the iOS toolchain and an iOS Flutter bridge source with a distinct provider identity.

## Physical qualification

Real Mac and real iPhone runs must prove Flutter presentation, VideoToolbox H.264/HEVC output, supported ProRes/alpha/HDR paths, lifecycle and memory-pressure handling, decoded artifact parity, and zero-readback/staging telemetry. Simulator or compile-only evidence is not production qualification.
