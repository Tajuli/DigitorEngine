# Apple production-native providers

This package supplies the shared Metal/VideoToolbox provider boundary for macOS and iOS while keeping their Flutter registrar and lifecycle evidence platform-specific.

## Repository-owned contract

- Metal-only timeline and device identity enforcement;
- IOSurface-backed CVPixelBuffer presentation and encode requirements;
- Flutter macOS/iOS texture registrar bridge requirement;
- VideoToolbox hardware encoder binding;
- Metal completion/shared-event synchronization;
- HDR/color/alpha attachment propagation;
- app lifecycle and resource-retirement safety;
- zero CPU readback and zero CPU staging;
- strict native-provider registry validation for separate macOS and iOS slots.

## Required Flutter plugin source

The engine does not vendor Flutter Apple embedding headers. A real plugin source must implement `AppleFlutterTextureBridge` using the macOS or iOS texture registrar and retain the exact GPU-owned frame/IOSurface generation until Flutter releases it. CPU pixel mapping or RGBA byte-buffer presentation is not accepted.

## Build

Use the Apple release preset with:

```bash
-DDIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE=/absolute/path/apple_texture_bridge.mm
-DDIGITOR_APPLE_PROVIDER_IDENTITY=digitor-apple-provider-v1
-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=$PWD/cmake/AppleNativeProvider.cmake
```

The provider links Metal, CoreVideo, IOSurface, VideoToolbox, CoreMedia and Foundation. A missing bridge, framework, provider identity, hardware encoder binding, lifecycle evidence, or IOSurface-backed pool fails closed.

## Physical qualification

A real Mac and real iPhone must separately prove Flutter presentation, H.264/HEVC output, supported ProRes on macOS, HDR/alpha attachment propagation, background/foreground handling, memory pressure, output decode-and-compare, and zero-readback/staging telemetry.
