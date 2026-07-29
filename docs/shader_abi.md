# Shader ABI

ABI version 1 uses DXC's Vulkan DX layout. Public parameter records must be standard-layout and are accepted only after `validate_layout` matches reflected set, binding, total size, field offset/size, and row/column-major decoration. Backends must not repack or reinterpret mismatches. CPU operations use stable operation IDs in `CpuKernelRegistry` and consume the same validated parameter bytes; unknown IDs fail.
