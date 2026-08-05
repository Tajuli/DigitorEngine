# Production Layer Compositing v1

DigitorEngine owns authoritative layer compositing for video, image, text, mask, chroma-key and generated-frame outputs. The subsystem supports normal, multiply, screen, overlay, darken, lighten, add, subtract and difference blend modes, per-layer opacity, straight-alpha input, premultiplied-alpha input, and selectable straight or premultiplied output.

Preview and export use the same deterministic RGBA32F reference path and frame digest. GPU callers use validated Vulkan, Direct3D 12, Metal or OpenGL ES dispatch packets. Once a GPU backend is selected, a missing recorder returns `backend_unavailable` and a failed recorder returns `dispatch_failed`; the engine never silently executes the CPU path.

Flutter owns layer order, visibility, blend-mode controls and opacity UI. DigitorEngine owns alpha math, blend equations and final pixels. The stable packed-float C ABI is suitable for Flutter FFI qualification and explicit CPU reference execution.
