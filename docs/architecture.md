# Architecture

## Design goals

- GPU-first execution
- CPU fallback
- Stable C ABI
- C++20 implementation
- One engine API for Windows, Android, iOS, and macOS
- Preview and export must eventually use the same shader graph
- Deterministic color-processing math
- Explicit ownership and lifetime rules

## High-level layers

```text
Flutter UI / Host App
        |
        v
Stable C API
        |
        v
Digitor Core
        |
        +-- Renderer abstraction
        |      +-- Vulkan
        |      +-- Metal
        |      +-- D3D12
        |      +-- OpenGL ES
        |      +-- CPU
        |
        +-- Future modules
               +-- Decode
               +-- Timeline
               +-- Shader graph
               +-- Color
               +-- Audio
               +-- Export
```

## Milestone 0.1.0

Only engine lifecycle, backend selection, context lifecycle, tests, and documentation are implemented.

## Milestone 0.3.0 resource-layer increment

The public resource model starts with opaque texture and buffer handles, explicit immutable
descriptors, checked allocation-size arithmetic, and context-scoped lifetime tracking. The CPU
backend provides actual zero-initialized storage. GPU backends deliberately reject resource
creation until their native devices, allocators, and synchronization primitives exist; a hidden
CPU allocation must never masquerade as a GPU resource.

Only `RGBA32_FLOAT` textures are accepted in this increment. This keeps the initial internal
working format aligned with the engine's 32-bit floating-point color requirement while format
conversion policy is designed separately.

## 0.4 native resources
Descriptor validation is shared by `RenderContext`; the selected `IRenderBackend` owns native creation/destruction. Native objects never enter the C ABI. CPU resources use byte storage. Vulkan binds selected device/host-visible memory and creates image views; D3D12 uses committed default/upload resources; Metal uses private/shared storage under ARC-safe bridge ownership; GLES requires a current EGL context for every creation and destruction call. Partial failures unwind acquired native objects.


## Software media boundary

The media module owns FFmpeg format, codec, packet, frame, scaler, and resampler lifetimes. It
normalizes decoded data at the boundary (microsecond timestamps, RGBA32F video, interleaved float
PCM audio), keeping FFmpeg types out of the public API and GPU/resource layers. Seeking flushes all
decoder state before new packets enter the pipeline.
