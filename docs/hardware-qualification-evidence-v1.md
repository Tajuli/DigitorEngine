# Production Hardware Evidence v1

This milestone defines the authoritative release evidence contract for real Vulkan, D3D12, Metal and OpenGL ES qualification.

A record is accepted only when it identifies a real device, GPU, driver, engine version and source commit; contains a SHA-256-addressed evidence artifact; proves GPU execution; reports no silent CPU fallback; satisfies soak, frame-count, error and performance thresholds; and preserves preview/export digest parity.

The validator returns `qualified` only when Windows, Android, macOS and iOS each provide one valid and unique record. A structurally valid partial bundle is `blocked`. Invalid data, duplicate devices/hashes, unsupported platform/backend pairs or failed thresholds are `invalid`.

Hosted CI qualifies the validator and ABI contract. It does not generate real-device evidence. Device-lab runs, signed attestations, driver captures, render digests and benchmark artifacts must be produced by the actual release environment and attached to the release candidate.
