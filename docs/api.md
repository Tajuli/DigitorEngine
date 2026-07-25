# Public C API

The public API lives in:

```text
include/digitor/digitor.h
```

## Lifecycle

- `digitor_initialize`
- `digitor_shutdown`
- `digitor_get_version`

## Renderer

- `digitor_get_renderer_info`

## Context

- `digitor_create_render_context`
- `digitor_destroy_render_context`

## Resource foundation

- `digitor_create_texture` / `digitor_destroy_texture`
- `digitor_create_buffer` / `digitor_destroy_buffer`

Resource descriptors are plain C structures and resource handles are opaque. A render context
owns the lifetime domain of every resource created from it; destroying a context with live
resources returns `DIGITOR_RESULT_RESOURCE_IN_USE`. The current resource-layer increment has a
real CPU allocation implementation for `RGBA32_FLOAT` textures and buffers. Native GPU resource
allocation is not yet implemented and returns `DIGITOR_RESULT_UNSUPPORTED` rather than silently
allocating host memory or claiming a GPU resource exists.

The C ABI is intentionally small so Flutter, Kotlin, Swift, C#, Rust, or any other host can bind to it.

On Windows, CMake propagates `DIGITOR_ENGINE_STATIC` to consumers when the default static library
is built. Shared-library builds continue to use `__declspec(dllexport)` and
`__declspec(dllimport)`. Consumers linking a manually packaged static library must define
`DIGITOR_ENGINE_STATIC` as part of that package's usage requirements.

## Resource API (0.4.0)
`digitor_create_texture`, `digitor_create_buffer`, and the additive `digitor_create_sampler` clear output handles before work. Descriptors are copied and immutable. Valid formats are `RGBA8_UNORM`, `BGRA8_UNORM`, `RGBA16_FLOAT`, and `RGBA32_FLOAT`; a backend may return `DIGITOR_RESULT_UNSUPPORTED` (not substitute a format). Zero sizes, unknown flags, and malformed sampler enums are invalid. Upload/staging buffers request host-visible/shared/upload memory; other resources request device-local/private/default memory. Destroy calls reject null, unknown, retired, and double-destroyed public handles. Destroy resources before their owning context.
Shutdown returns `DIGITOR_RESULT_RESOURCE_IN_USE` while a context remains, so a native device can never be invalidated beneath live resources.
