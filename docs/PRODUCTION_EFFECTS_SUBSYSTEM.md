# DigitorEngine Production Effects Subsystem v1

## Scope

This subsystem promotes the existing deterministic effect kernels into a stable production-facing framework without recreating the kernels.

Built-in IDs:

- `effect.gaussian_blur`
- `effect.sharpen`
- `effect.glow`
- `effect.lens_distortion`
- `effect.noise`
- `effect.film_grain`
- `effect.chromatic_aberration`
- `effect.vignette`
- `effect.motion_blur`

## Contracts

- Stable effect IDs and categories
- Validated amount, radius, angle, and seed
- Ordered multi-effect stacks
- Versioned stack serialization
- Preview, balanced, and export quality policies
- Optional R32F matte compositing
- Alpha preservation
- Deterministic CPU and command-recorder execution
- Fail-closed handling for unknown effects, invalid ranges, invalid mattes, and malformed serialized data

## Quality policy

Preview and balanced modes only cap expensive spatial radius. They do not change effect identity, parameter serialization, ordering, alpha behavior, or seed. Export quality uses the full validated radius.

## Preview/export parity

Preview and export consume the same registry, stack, and effect settings. A project stores stable effect IDs and parameter values rather than backend-specific objects.

## Deferred native backend work

The existing `apply_effect_gpu` command contract is reused in v1. Backend-specific multi-pass texture kernels, pipeline objects, temporal motion-vector effects, and zero-copy native surface qualification remain separate native-GPU milestones. No CPU fallback is silently selected by the registry.
