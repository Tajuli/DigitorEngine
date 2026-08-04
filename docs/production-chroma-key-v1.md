# Production Chroma Key v1

This feature removes green, blue, or custom-color screens through one authoritative engine path. It includes similarity/softness matte generation, edge shrink, spill suppression, premultiplied-alpha output, deterministic preview/export parity, GPU dispatch contracts for Vulkan/D3D12/Metal/GLES, and a stable RGBA32F C ABI for Flutter.

GPU requests never silently execute on CPU. The reference processor is used for deterministic qualification and CPU fallback only when CPU is explicitly selected by the caller.
