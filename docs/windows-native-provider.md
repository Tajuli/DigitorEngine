# Windows native provider package

This package is the repository-owned Windows provider boundary for DigitorEngine.

## Implemented in this change

- strict Windows-only CMake target;
- required Flutter Windows include/library inputs;
- D3D12, DXGI and Media Foundation linkage;
- stable embedded provider identity;
- runtime context containing Flutter registrar, D3D12 queue/device, DXGI adapter and renderer identity;
- Media Foundation startup probe;
- hardware H.264 encoder enumeration through `MFTEnumEx`;
- D3D12/device identity validation;
- fail-closed provider factory when runtime identities or capabilities are incomplete.

## Not yet production-qualified

The provider must not be marked release-ready until the following repository-owned operations are implemented and tested:

1. register/unregister the exact GPU-backed texture through the Flutter Windows texture registrar;
2. retain each `ProcessedGpuFrame` until Flutter consumption completes;
3. submit D3D12 resources to a real Media Foundation, NVENC or QSV encoder session;
4. execute Vulkan external-memory/semaphore conversion when Vulkan is selected;
5. publish synchronization and zero-readback telemetry from the real operations;
6. pass Windows native-release CI and physical GPU output decode-and-compare.

The current probe/package is a prerequisite for those operations, not a substitute for them.
