# Android Physical Qualification Report

Date: 2026-08-07
Commit: 4983bb24e3e59be3e16ace01b16da11e5523b2e8

## Environment
- Platform: Android 12 / API 31
- Device: Symphony Z60
- Hardware: ums9230_1h10
- GPU: Mali-G57
- Renderer Backend: Vulkan
- Qualification: Physical Android device

## Results
- Real-media fixture generation: PASS
- Qualification shader compilation: PASS
- Android arm64 harness configure: PASS
- Android arm64 harness build: PASS
- Physical-device harness push: PASS
- Physical-device runtime execution: PASS
- Hardware AVC decode: PASS (`c2.unisoc.avc.decoder`)
- MediaCodec to AHardwareBuffer path: PASS
- Vulkan external AHardwareBuffer import: PASS
- GPU submission: PASS
- GPU timestamp validation: PASS
- Hardware AVC encode: PASS (`c2.unisoc.avc.encoder`)
- Hardware encoder surface startup: PASS
- Preview/export parity: PASS

## Verified runtime metrics
- CPU_READBACKS=0
- CPU_REUPLOADS=0
- FALLBACK_DISPATCHES=0
- INTERMEDIATE_READBACKS=0
- INTERMEDIATE_REUPLOADS=0
- VALIDATION_READBACKS=2

## Preview/Export validation
- PREVIEW_DIGEST=4498201089720295185
- EXPORT_DIGEST=4498201089720295185
- PREVIEW_EXPORT_PARITY=PASS

## Overall verdict
Android release qualification PASSED on a real Symphony Z60 physical device.

The qualified runtime demonstrated the intended GPU-first path with hardware MediaCodec decode, AHardwareBuffer import into Vulkan, real GPU submission/timestamps, hardware encode, zero CPU fallback/intermediate transfer counters, and exact preview/export digest parity.

## Teardown note
After the qualification result had already reported `ANDROID_PHYSICAL_RELEASE_QUALIFICATION=PASS` and `ANDROID_RELEASE_STATUS=PASS`, the standalone qualification process emitted a FORTIFY `pthread_mutex_lock called on a destroyed mutex` message during process teardown. This occurred after all release-gate evidence had been collected and did not change the qualification verdict. The teardown warning remains a cleanup issue for the standalone qualification harness and is not counted as a GPU/media/parity failure.

This report documents physical qualification performed on a real Android device. Windows physical qualification is also recorded separately. macOS and iOS physical qualification remain separate release gates.
