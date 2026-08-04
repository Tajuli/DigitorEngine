# Editor Visual Feature Source Audit v1

## Existing foundations reused

- RenderGraph owns pass ordering, resources, barriers and transient lifetimes.
- NodeGraph owns serial/parallel visual processing, bypass and frame caching.
- Timeline owns clip scheduling and edit history.
- Existing native color pipelines remain unchanged, including qualified Primary Wheels.
- Preview/export qualification already uses deterministic digest and per-pixel error concepts.

## Gaps addressed by this milestone

- No single engine-owned state model existed for transform, crop, text/font, chroma key and stabilization controls.
- App-facing commands did not share one undo/redo-safe visual editing contract.
- There was no dedicated qualification gate combining GPU-first enforcement, preview/export parity, per-pixel color error and preview frame-time budget.
- UI-only features lacked an explicit boundary identifying what stays in Flutter and what must be authoritative in the engine.

## App/engine boundary

The Flutter app retains Transform panels, crop handles, text boxes, font/color pickers, buttons, sliders, timeline/node UI, preview transport, project browser, import/export screens, progress UI and touch recognition. DigitorEngine owns the state those controls modify, command validation, undo/redo snapshots, GPU render policy, deterministic preview/export output and color/performance qualification.

## Follow-on implementation layers

This v1 contract is the required stable integration surface. Native backend kernels, FFI functions, font shaping/rasterization, optical-flow analysis and project serialization should bind to this contract without duplicating visual state in the app.
