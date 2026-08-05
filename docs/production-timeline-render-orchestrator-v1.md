# Production Timeline Render Orchestrator v1

This subsystem provides one deterministic frame-level coordinator for DigitorEngine timeline preview and export. It executes keyframe evaluation, source decode, transform/crop, effects, transitions, compositing, output and optional audio mixing in a fixed order while carrying a dependency digest between stages.

Preview and export requests use the same stage executor contract and produce the same render digest for identical project revision, timeline time, frame index and subsystem outputs. The mode is intentionally excluded from the digest so preview/export parity can be asserted directly.

A selected GPU path never silently falls back to CPU. Any stage can report `backend_unavailable`, and the orchestrator stops immediately with the exact failed stage. Other stage failures are reported as `stage_failed`.

The stable C ABI allows Flutter or another host to provide the stage callback while DigitorEngine owns ordering, validation, failure propagation and final parity digest generation.