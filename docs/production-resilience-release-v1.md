# Production Resilience & Release v1

This milestone adds a bounded backend/device-loss recovery contract and deterministic release-manifest validation.

## Recovery guarantees

- CPU is never treated as a recoverable GPU backend.
- Device loss invalidates in-flight frame resources and optionally the pipeline cache generation.
- Recovery attempts are bounded; exhaustion is explicit.
- Successful recreation advances the device generation before new work may resume.
- Recovery snapshots expose attempts, invalidated frames, generations, status, and a deterministic digest.
- The C ABI contains allocation and callback exceptions.

## Release guarantees

A valid release manifest must include a semantic version, non-zero ABI major, source commit, at least one compiled GPU backend, required preview/export parity, and the no-silent-CPU-fallback policy.

## Honest qualification boundary

CI validates state transitions, ABI translation, deterministic metadata, and cross-platform compilation. It does not claim real Vulkan, D3D12, Metal, or GLES device-loss injection on hosted runners. Hardware evidence remains required before a production release can mark those backend rows qualified.
