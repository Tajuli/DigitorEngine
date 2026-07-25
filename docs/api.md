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
