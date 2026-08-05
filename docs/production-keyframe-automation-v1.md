# Production Keyframe Animation & Parameter Automation v1

DigitorEngine now owns deterministic evaluation of timeline-driven scalar parameters for transform, crop, opacity, blend controls, masks, transitions, color/effect parameters and audio automation.

The subsystem supports hold, linear, ease-in, ease-out, ease-in-out and cubic-bezier interpolation; strict monotonic keyframe validation; configurable clamping before the first and after the last keyframe; deterministic evaluation digests; and a stable C ABI for Flutter FFI.

Preview and export must evaluate the same track at the same media timestamp. The app owns keyframe editing UI, graph handles and undo/redo commands; DigitorEngine owns authoritative interpolation math and evaluated parameter values.
