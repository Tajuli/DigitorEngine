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

The C ABI is intentionally small so Flutter, Kotlin, Swift, C#, Rust, or any other host can bind to it.
