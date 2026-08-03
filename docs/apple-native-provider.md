# Apple production-native provider

This package provides the shared Metal/CoreVideo/VideoToolbox implementation boundary for macOS and iOS while preserving separate platform identities in the strict native-provider registry.

## Repository-owned contract

- Metal-only timeline and device identity enforcement;
- CVMetalTextureCache and IOSurface-backed CVPixelBuffer requirements;
- Metal completion synchronization;
- Flutter macOS/iOS texture bridge requirement;
- VideoToolbox hardware encoder binding;
- color, HDR and alpha attachment capability reporting;
- zero CPU readback and zero CPU staging enforcement;
- separate macOS and iOS provider identities.

## Required Flutter plugin source

The engine does not vendor Flutter Apple embedding sources. A real macOS or iOS plugin source must implement `AppleFlutterTextureBridge` with the platform texture registrar and retain the exact `ProcessedGpuFramePtr`/CVPixelBuffer generation until Flutter releases it.

The bridge must not copy pixels through Dart, map Metal textures to CPU memory, or allocate a CPU RGBA staging buffer.

## Release configure

```bash
cmake --preset macos-native-release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/AppleNativeProvider.cmake" \
  -DDIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE=/path/to/macos_texture_bridge.mm \
  -DDIGITOR_APPLE_PROVIDER_IDENTITY=digitor-macos-provider-v1
```

For iOS, use the iOS toolchain/preset and an iOS Flutter texture bridge source with a distinct provider identity.

## Physical qualification

Real Mac and iPhone runs must prove Flutter presentation, H.264/HEVC and supported ProRes output, HDR/alpha attachment propagation, background/lifecycle handling, output decode-and-compare, long export stability, and zero-readback/staging telemetry. Simulator or compile-only evidence is not production qualification.
