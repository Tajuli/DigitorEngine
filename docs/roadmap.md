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

## v4.6.1 native grade qualification

The source-level audit and exact qualification truth table are maintained in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md). This host audit did
not execute qualifying Vulkan, D3D12, Metal, GLES, Android, or iOS hardware.
Native compilation is not reported as pixel validation; FP16 is unsupported.
The internal provenance/failure seams prove that failures do not silently run
the CPU reference in non-hardware tests.

## v4.7 RGB Curves qualification remaining

The curve math, CPU reference, LUT cache, and CPU graph contract are implemented. Release requires real backend shaders/resources/dispatch on Vulkan, D3D12, Metal, and GLES, native provenance and numerical/performance qualification. Wheels, qualifier, 3D LUT, UI, timeline, codecs, and export formats are explicitly out of this item.

The v4.7 implementation now has its canonical shader ABI, native LUT cache, graph node and provenance contract. Remaining release gates are native adapter wiring on Vulkan/D3D12/Metal/GLES, preview texture routing, failure execution, hardware numerical reports, and performance capture. No compilation-only result counts as verification.
