# Editor Visual Feature Contract v1

This milestone defines the engine-owned state and command contract required by the Digitor app's Transform panel, crop handles, text input, font picker, chroma-key color picker, stabilization controls, sliders/numeric fields, timeline and node-graph commands, preview controls, undo/redo, project import/export, progress reporting and touch gestures.

The Flutter app owns presentation and gesture recognition. DigitorEngine owns authoritative visual state, validation, undo/redo semantics, render scheduling and preview/export parity. Features that change pixels must not be implemented independently in Flutter.

## Rendering requirements

- GPU backends must execute on their selected GPU path; silent runtime CPU fallback is rejected.
- Explicit CPU selection remains a supported fallback path.
- Preview and export must produce matching deterministic frame digests.
- Maximum and mean per-channel error are qualified against per-pixel limits.
- Preview samples must stay inside the configured frame-time budget.
- Color metadata must survive preview and export processing.

## Feature placement

- Transform and crop are spatial stages before color grading and compositing.
- Stabilization supplies a correction transform before the user transform.
- Chroma key generates/refines alpha and performs despill before compositing.
- Text is an engine-rendered overlay/generator; the app only supplies UTF-8 text, font selection and style controls.
- Timeline UI, node graph UI, project browser, import/export screens, progress indicators and touch gestures remain app UI, but communicate through engine commands and stable FFI bindings.

This contract is additive and does not redesign the qualified Primary Wheels implementation.
