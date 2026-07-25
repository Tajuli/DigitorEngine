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
