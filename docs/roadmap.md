# Roadmap

## Production stabilization (current)

Build/test/package qualification, strict engine warnings, cross-platform FFmpeg discovery, generated
real-media tests, installed consumers, C-boundary auditing, and explicit determinism/plugin specifications.
This work adds no rendering feature. Version numbers are not completion or production evidence.

## Remaining qualification

- Establish ABI versioned/size-tagged structs and compatibility baselines before promising ABI stability.
- Make concurrent handle destruction safe and complete exception-containment tests for every C entry point.
- Run native GPU pixel, device-loss, leak, and driver matrices separately from deterministic CPU CI.
- Independently validate export interoperability and performance on supported operating systems.
- Implement plugin loading only after security review of `plugin_architecture.md`.

Capabilities retain the classifications in `implementation_status.md` and `production_readiness.md`.

## Native shader toolchain follow-up
Complete native DXIL reflection/root-signature integration, SPIRV-Cross Metal/GLES translation, backend pipeline factories, and persistent binary cache before declaring this milestone production-complete. Hardware jobs remain separate and must name device/driver versions.

## After v4.5

Native ColorTransformGraph compilation/qualification and metadata-aware plane
upload precede later RGB Curves, Primary Wheels, Log Wheels, HSL Qualifier and
LUT milestones. HDR display mapping, highlight roll-off and gamut compression
remain future work.
