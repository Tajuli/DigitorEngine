# Windows D3D12 Built-in Effect Shaders v1

This package closes the concrete Windows shader gap for the merged native effects runtime and D3D12 provider.

## Repository-owned GPU path

- one strict HLSL compute package
- D3D12 root signature and compute PSO
- shader-visible SRV/UAV descriptor heap
- per-pass root constants
- 8x8 compute dispatch
- RGBA8, BGRA8 and RGBA16F resources
- alpha preservation
- no staging resource, readback or CPU pixel access

## Stable built-in coverage

- `effect.gaussian_blur`
- `effect.sharpen`
- `effect.glow`
- `effect.lens_distortion`
- `effect.noise`
- `effect.film_grain`
- `effect.chromatic_aberration`
- `effect.vignette`
- `effect.motion_blur`

Blur, glow and motion blur consume the two-pass schedule supplied by the D3D12 provider. Other effects use one pass. The exact `EffectStack` parameters and seed are passed as root constants, so preview and export share effect identity and deterministic parameter state.

## Failure policy

Creation or dispatch fails closed for a missing device, shader compile failure, root-signature/PSO/heap creation failure, unknown effect ID, aliased resources, or resource geometry/format mismatch.

## Qualification boundary

Physical Windows hardware still must verify numerical parity against the reference kernels, SDR/HDR output, alpha, preview/export identity, zero-copy telemetry, memory pressure, device loss and long-run execution. This source package does not by itself constitute physical-device qualification.
