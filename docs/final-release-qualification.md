# Final release qualification

This is the final gate after source integration. It does not replace platform hardware testing and must not convert compile-only, emulator, simulator, callback, or synthetic-frame evidence into a production claim.

## Exact-commit rule

Every CI result, native-provider build, physical-device run, output artifact, signature, and retained report must reference the exact commit selected for release. A hardware report from an earlier commit is not accepted for a later tag.

## Required source evidence

- canonical package/runtime/CMake version agreement;
- full required CI green for the exact commit;
- install-consumer test;
- public C ABI test;
- public C++ API test;
- production-native timeline, Flutter texture and encoder bindings for Windows, Android, macOS and iOS;
- backend/snapshot and device-identity agreement;
- native synchronization and zero-copy telemetry bindings;
- Windows Vulkan external-memory/semaphore encoder interop evidence when that path is claimed.

## Required physical evidence per platform

- real physical device, never simulator/emulator-only;
- native preview presentation;
- native hardware export completion;
- exported artifact retained by identity/hash and decoded successfully;
- golden-frame parity within the documented threshold;
- audio/video sync and VFR validation;
- cancellation and recovery/device-loss-or-codec-reset validation;
- long-run playback/export validation;
- zero CPU readback and zero CPU staging telemetry.

Platforms:

1. Windows: D3D12 and claimed Vulkan paths on real compatible hardware.
2. Android: Vulkan and GLES fallback paths on representative physical devices.
3. macOS: Metal, IOSurface/CVPixelBuffer and VideoToolbox on a real Mac.
4. iOS: Metal, CVPixelBuffer and VideoToolbox on a real iPhone.

## Artifacts

A release is accepted only after distributable artifacts are built and signed and the qualification report is retained with device, backend, encoder, codec/container, artifact hash and exact commit identities.

`validate_release_qualification_bundle()` is fail-closed: a missing platform, simulator evidence, commit mismatch, missing output decode, failed parity/sync/recovery/long-run check, CPU readback/staging, or unsigned artifact rejects the release.
