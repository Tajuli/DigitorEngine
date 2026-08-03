# Apple Metal built-in effects v1

This package supplies the repository-owned MSL compute implementation used by the Apple native effects provider on macOS and iOS.

## Built-in effect IDs

- `effect.gaussian_blur`
- `effect.sharpen`
- `effect.glow`
- `effect.lens_distortion`
- `effect.noise`
- `effect.film_grain`
- `effect.chromatic_aberration`
- `effect.vignette`
- `effect.motion_blur`

The package compiles one deterministic Metal library, creates and retains its compute pipeline, validates the exact `MTLDevice`, and records into the provider-owned command buffer. It never creates a second queue, maps textures to CPU memory, or introduces a staging upload/readback path.

## Formats and scheduling

- RGBA8 and BGRA8 SDR
- RGBA16F HDR working surfaces
- alpha preservation
- two-pass blur, glow and motion blur
- single-pass remaining effects
- preview/balanced/export quality constants
- deterministic noise and grain seed

## Qualification

`Apple Metal Effects Qualification` creates a real `MTLDevice`, compiles the MSL package, executes every built-in effect on SDR and HDR textures, waits for GPU completion, and requires zero native-runtime CPU readback, re-upload and fallback counters.

A GitHub-hosted Mac pass is a source/runtime gate. Final release evidence still requires the exact release commit on supported physical Macs and real iPhones, including preview/export numerical comparison, HDR/alpha propagation, cancellation, lifecycle/background handling, memory pressure and long-run stability.
