# Production Spatial Compositor v1

This milestone turns the editor visual contract from PR #250 into executable engine processing.

## Completed

- deterministic RGBA32F transform, crop, anchor, rotate, scale, flip, opacity and nearest/bilinear sampling
- chroma-key matte generation with softness and green-spill suppression
- premultiplied-style source-over compositing plus add, multiply and screen modes
- stabilization correction application from cached motion samples
- linear animated scalar evaluation
- shared `render_layer` path for preview and export parity
- stable frame digest qualification
- C ABI suitable for Flutter FFI
- explicit rejection when a requested GPU backend is unavailable and CPU fallback is disabled
- cross-platform warning-clean qualification on Linux, Windows and macOS

## GPU policy

The API records whether a GPU backend is available and never silently uses CPU when fallback is disabled. The deterministic scalar implementation is the reference path and qualification oracle. Native Vulkan, D3D12, Metal and GLES kernels can implement the same contract without changing Flutter-facing state or output rules.

## Scope boundary

This milestone does not claim optical-flow analysis, HarfBuzz/FreeType text shaping, glyph-atlas rendering or hardware GPU execution. Stabilization consumes precomputed motion tracks; text remains represented by the merged editor visual state until the dedicated shaping/raster milestone. These are separate heavy subsystems and are not disguised as completed.
