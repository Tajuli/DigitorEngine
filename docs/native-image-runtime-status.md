# Native image runtime status

The engine-side native image runtime is complete when paired with a platform GPU service provider: backend selection and lock, WIC/ImageIO/AImageDecoder codec adapters, deterministic EXIF orientation, ICC identity propagation, alpha/JPEG flatten rules, memory limits, progress/cancellation, shared video graph processing, preview/export routing, and CPU-only fallback selection.

Platform plugins must still supply their active D3D12/Vulkan/Metal/OpenGL ES upload, resize, graph, preview and final encode/readback callbacks. Android has no NDK JPEG/PNG/WebP encoder API; its Java/Kotlin encoder callback is therefore required. Once a GPU provider is selected, provider or codec failure is returned and never switches processing to CPU.
