# Windows provider status

This change establishes the native Windows build boundary required before a production provider can be accepted.

Implemented here:

- real Flutter Windows SDK header requirement;
- D3D12, DXGI 1.6, Media Foundation and Vulkan SDK detection;
- Media Foundation, NVENC and oneVPL/Quick Sync encoder selection gates;
- required Windows libraries and compile definitions;
- fail-closed configuration for missing SDKs;
- Windows-specific top-level CMake integration.

Not claimed by this change:

- Flutter texture registrar implementation;
- D3D12/Vulkan command submission implementation;
- Media Foundation/NVENC/QSV encoder session implementation;
- physical Windows GPU qualification.

Those implementation files must be added under `platform/windows/` and compiled through the provider source option. The source-release validator must remain closed until that code and exact-commit CI evidence exist.
