# Windows production-native provider

The repository now contains a Windows provider source selected automatically by the `windows-native-release` preset.

## Native initialization

The provider performs real Windows API initialization and fails closed when any required component is unavailable:

- COM multithreaded apartment;
- DXGI 1.6 factory;
- non-software hardware adapter;
- D3D12 device on that adapter;
- Media Foundation startup;
- real Flutter Windows GPU texture API binding;
- shared engine device and adapter identity.

The provider links `d3d12`, `dxgi`, `mf`, `mfplat`, `mfuuid` and `ole32` through the common native-provider CMake module.

## Flutter texture boundary

`WindowsFlutterGpuTextureApi` must be populated by the Digitor Flutter Windows plugin with the actual Flutter Windows texture registrar functions. The provider does not expose or accept a CPU pixel-buffer presentation path.

A release provider is invalid when the registrar, register, frame-available or unregister functions are absent.

## Hardware export

Media Foundation is initialized as the guaranteed Windows hardware-encoder API. Existing NVENC and Quick Sync adapter contracts remain selectable when their native packages are available. Software fallback remains forbidden for hardware-required jobs.

## Vulkan/DXGI

Vulkan is not marked qualified merely because extension names are present. A Vulkan release must retain evidence of:

1. DXGI external-memory import/export on the selected adapter LUID;
2. external semaphore/fence execution;
3. Vulkan frame conversion/export invoked by encoder submission;
4. validated D3D12 encoder resource;
5. zero CPU readback and zero CPU staging.

Until that execution evidence is supplied, a Vulkan-required provider reports not ready. D3D12 remains the qualified Windows native path.

## Release validation

Run on Windows:

```powershell
cmake --preset windows-native-release
cmake --build --preset windows-native-release
ctest --preset windows-native-release
```

Physical qualification still requires Flutter presentation, real hardware encoding, output decode-and-compare, device-loss recovery and long-run zero-copy telemetry on the exact release commit.
