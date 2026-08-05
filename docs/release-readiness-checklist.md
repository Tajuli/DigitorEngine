# DigitorEngine Production Release Readiness Checklist

A production release must not be declared complete until every applicable item is evidenced by source and qualification.

## Runtime and correctness

- [x] Explicit preview/export session lifecycle
- [x] Cancellation and progress contract
- [x] CPU/GPU resource-budget enforcement
- [x] Failed-frame and failed-stage propagation
- [x] Preview/export digest parity guard
- [x] C ABI exception containment
- [ ] Real application project renders through the complete Flutter-to-engine path
- [ ] Long-duration and high-resolution media soak results are archived
- [ ] Device-loss recovery is qualified on each native GPU backend

## GPU execution

- [x] GPU-selected execution must not silently fall back to CPU
- [ ] Every production visual subsystem has real Vulkan execution evidence
- [ ] Every production visual subsystem has real Direct3D 12 execution evidence
- [ ] Every production visual subsystem has real Metal execution evidence
- [ ] Every production visual subsystem has real OpenGL ES execution evidence
- [ ] Preview and export are pixel-compared with real media on every backend

## Platform and distribution

- [ ] Windows package/install/upgrade/uninstall qualification
- [ ] Android AAR/ABI packaging and real-device qualification
- [ ] macOS framework/package and notarization qualification
- [ ] iOS framework/package and real-device qualification
- [ ] Public headers, symbols and semantic-version compatibility reviewed
- [ ] README, changelog, version and release notes agree

Unchecked items are explicit production gaps, not implied completed work.