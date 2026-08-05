# Production Video Stabilization v1

This feature adds deterministic motion smoothing, horizon lock, adaptive crop zoom, rolling-shutter compensation metadata, Vulkan/D3D12/Metal/GLES dispatch contracts, preview/export parity, and a stable C ABI for Flutter integration.

GPU requests never silently fall back to CPU. The CPU planner is deterministic and must be selected explicitly. Native backends consume the same per-frame transform plan for preview and export.
