# Windows production-native provider

The Windows release provider must compile against the real Flutter Windows embedding and native GPU/encoder SDKs. It is not valid to satisfy the provider gate with mock callbacks or a compile-only shim.

## Required components

- Flutter Windows ephemeral SDK containing `flutter_windows.h`
- Windows 10/11 SDK with D3D12, DXGI 1.6 and Media Foundation headers/libraries
- Vulkan SDK headers
- one encoder implementation:
  - Media Foundation hardware MFT (default),
  - NVIDIA Video Codec SDK for NVENC, or
  - oneVPL for Intel Quick Sync

## Configure

Use the Windows-specific top-level include so both the generic provider source and native Windows SDK gates attach to `digitor_engine`:

```powershell
cmake -S . -B build/windows-native -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/WindowsNativeProviderTopLevel.cmake" `
  -DDIGITOR_REQUIRE_NATIVE_PLATFORM_PROVIDER=ON `
  -DDIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE="$PWD/platform/windows/windows_native_provider.cpp" `
  -DDIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="digitor-windows-native-v1" `
  -DDIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR="<flutter-project>/windows/flutter/ephemeral" `
  -DDIGITOR_WINDOWS_ENCODER=media_foundation
```

For NVENC also set `DIGITOR_NVENC_SDK_ROOT`. For Quick Sync install oneVPL and set `DIGITOR_WINDOWS_ENCODER=qsv`.

Configuration fails if Flutter headers, D3D12/DXGI, Media Foundation, Vulkan, or the selected encoder SDK are unavailable. Passing this gate proves that the real provider can be compiled and linked; physical GPU execution and M6 evidence remain separate release requirements.
