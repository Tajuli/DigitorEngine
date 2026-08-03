# Apple production-native provider

This package supplies the shared Metal/IOSurface/VideoToolbox provider boundary for macOS and iOS while preserving distinct platform identities in the strict provider registry.

## Repository-owned contract

- Metal-only timeline, preview and export backend matching;
- CVPixelBuffer/IOSurface and CVMetalTextureCache requirements;
- Flutter macOS/iOS texture bridge requirement;
- VideoToolbox hardware encoder binding;
- color, HDR and alpha attachment propagation;
- lifecycle-safe frame ownership;
- zero CPU readback and zero CPU staging.

## Flutter bridge

The engine does not vendor Flutter Apple embedding sources. A real macOS or iOS plugin source must implement `AppleFlutterTextureBridge`, register IOSurface/CVPixelBuffer-backed textures, and retain the exact `ProcessedGpuFramePtr` until Flutter releases the texture generation. CPU RGBA conversion is forbidden.

## Release configure

```bash
cmake --preset macos-native-release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/AppleNativeProvider.cmake" \
  -DDIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE=/path/to/apple_texture_bridge.mm \
  -DDIGITOR_APPLE_PROVIDER_IDENTITY=digitor-macos-provider-v1
```

Use the iOS toolchain and `ios-native-release` preset for iPhone builds. Physical Mac and iPhone qualification must prove Flutter presentation, VideoToolbox output, decoded parity, lifecycle/memory-pressure behavior and zero-readback telemetry.
