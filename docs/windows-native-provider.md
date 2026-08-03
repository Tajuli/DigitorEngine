# Windows production-native provider

This provider assembles the existing production timeline, preview and hardware-encode contracts without introducing a CPU pixel path.

## Accepted paths

- D3D12 timeline → external GPU Flutter texture → D3D12 hardware encoder resource
- Vulkan timeline → DXGI external memory/semaphore → external GPU Flutter texture and D3D12 encoder resource

## Required implementation

The embedding must provide a Windows Flutter external-GPU-texture extension that accepts and retains the exact D3D12/Vulkan-backed `ProcessedGpuFrame`. The stock Flutter Windows pixel-buffer texture callback is intentionally rejected because it requires CPU-readable pixels and would violate the zero-copy requirement.

The encoder host must use a real Windows hardware session through Media Foundation, NVENC or Quick Sync and accept D3D12 resources after native fence synchronization. Software transforms, CPU RGB/YUV conversion, staging textures mapped to the CPU, and mid-session fallback are forbidden.

## Build gate

`cmake/WindowsNativeProvider.cmake` requires the external texture extension header and library and links the provider against Media Foundation, DXGI and D3D12. Missing dependencies fail configuration rather than silently enabling a copied preview path.

## Qualification boundary

This source establishes the strict Windows provider package and rejects incomplete integrations. Release qualification still requires a Windows hardware runner to prove actual Flutter presentation, encoded output decode-and-compare, Vulkan/DXGI synchronization where selected, long-run stability, and zero CPU readback/staging telemetry.
