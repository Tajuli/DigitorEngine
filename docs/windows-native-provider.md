# Windows production-native provider

This package is the Windows implementation boundary for the merged native-provider release gate.

## Included in the repository

- `WindowsNativeProviderBindings` and fail-closed provider factory;
- shared D3D12/Vulkan device-identity enforcement;
- timeline, Flutter texture and encoder implementation evidence;
- Windows SDK, DXGI 1.6, D3D12 and Media Foundation capability requirements;
- Vulkan external-memory/external-semaphore requirements;
- zero CPU readback/staging requirements;
- Windows SDK link package.

## Required Flutter plugin source

The engine deliberately does not vendor Flutter. A Windows Flutter plugin target must implement `WindowsFlutterTextureBridge` with the real `FlutterDesktopTextureRegistrarRef`/Flutter Windows texture APIs and retain the exact `ProcessedGpuFramePtr` until Flutter releases the texture generation.

Release configuration rejects a missing bridge source:

```powershell
cmake --preset windows-native-release `
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$PWD/cmake/WindowsNativeProvider.cmake" `
  -DDIGITOR_WINDOWS_FLUTTER_BRIDGE_SOURCE="C:/path/to/digitor_windows_texture_bridge.cpp" `
  -DDIGITOR_WINDOWS_PROVIDER_IDENTITY="digitor-windows-provider-v1"
```

The bridge must not map GPU pixels or create CPU RGBA buffers.

## Qualification boundary

This source package does not by itself prove physical NVENC/Media Foundation/QSV, Flutter presentation or Vulkan/DXGI execution. Windows CI and a real Windows GPU runner must still compile the provider, export a retained artifact, decode-and-compare it, and show zero readback/staging telemetry.
