# Apple production-native provider

This package supplies the macOS and iOS implementation boundary for the production native-provider release gate.

## Repository-owned contract

- Metal-only timeline and device identity enforcement;
- IOSurface/CVPixelBuffer and CVMetalTextureCache requirements;
- Flutter macOS/iOS texture bridge requirement;
- VideoToolbox hardware encoder binding;
- Metal completion synchronization;
- HDR/color attachment propagation;
- lifecycle-safe resource handling;
- zero CPU readback and zero CPU staging enforcement;
- distinct macOS and iOS provider registry identities.

## Required Flutter plugin source

The engine does not vendor Flutter Apple embedding sources. A Flutter plugin target must implement `AppleFlutterTextureBridge` using the real macOS or iOS texture registrar and retain the exact `ProcessedGpuFramePtr`/CVPixelBuffer generation until Flutter releases it.

It must not map Metal textures, create CPU RGBA buffers, or replace IOSurface-backed frames with copied pixel buffers.

## Release configure

```bash
cmake --preset macos-native-release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/AppleNativeProvider.cmake" \
  -DDIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE=/path/to/digitor_macos_texture_bridge.mm \
  -DDIGITOR_APPLE_PROVIDER_IDENTITY=digitor-macos-provider-v1
```

Use the iOS toolchain and `ios-native-release` preset for the iOS package with a separate provider identity and iOS Flutter bridge source.

## Physical qualification

A real Mac and real iPhone must prove Flutter presentation, VideoToolbox H.264/HEVC output, supported ProRes paths, HDR/alpha metadata, lifecycle interruption, output decode-and-compare, long export stability, and zero-readback/staging telemetry. Simulator or compile-only evidence is not production qualification.
