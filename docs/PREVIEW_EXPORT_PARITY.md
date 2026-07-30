
## v5.1 preview quality contract

Preview performance is controlled only by execution dimensions and source choice; the immutable color graph is unchanged.

- `Full`: viewport resolution.
- `Half`: half width and height.
- `Quarter`: quarter width and height.
- `Proxy`: configured proxy/decode-scaled dimensions, never upscaled beyond the viewport.
- `Adaptive`: deterministic half-resolution baseline. A future frame-time controller may change the selected quality between frames, but it must never mutate color parameters, operation order, precision, or color metadata.

Export always resolves the `Original` source through `build_color_render_plan(RenderPurpose::Export, ...)`. Preview caches are invalidated when quality changes, preventing a reduced-quality frame from being reused as a full-quality frame.
