# Android production-native provider

This package is the Android implementation boundary for the production native-provider release gate.

## Repository-owned contract

- Vulkan and OpenGL ES timeline/device identity enforcement;
- AHardwareBuffer and native-fence synchronization requirements;
- Flutter Android texture bridge requirement;
- MediaCodec hardware input-surface encoder binding;
- zero CPU readback and zero CPU staging enforcement;
- strict native-provider identity and registry validation.

## Required Flutter plugin source

The engine does not vendor Flutter Android embedding sources. A Flutter plugin target must implement `AndroidFlutterTextureBridge` using the real Android `TextureRegistry`, `SurfaceTexture`/image texture APIs, and retain the exact `ProcessedGpuFramePtr` until the texture generation is released.

It must not call `AHardwareBuffer_lock`, map GPU pixels, create an RGBA byte buffer, or copy frames through Dart.

## Release configure

```bash
cmake --preset android-native-release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/AndroidNativeProvider.cmake" \
  -DDIGITOR_ANDROID_FLUTTER_BRIDGE_SOURCE=/path/to/digitor_android_texture_bridge.cpp \
  -DDIGITOR_ANDROID_PROVIDER_IDENTITY=digitor-android-provider-v1
```

## Physical qualification

A real device must prove Vulkan and GLES paths as applicable, MediaCodec hardware output, decoded artifact parity, lifecycle interruption handling, and zero-readback/staging telemetry. Emulator or compile-only evidence is not production qualification.
