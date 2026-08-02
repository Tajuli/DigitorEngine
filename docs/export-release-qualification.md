# Export M6 release qualification

This gate does not create another renderer, color pipeline, timeline, decoder, encoder, or muxer. It qualifies the existing M1–M5 export path and rejects unsupported or unproven release claims.

## Required evidence per platform

| Platform | Required GPU path | Required physical evidence |
|---|---|---|
| Windows | D3D12 or qualified Vulkan interop → NVENC/QSV/Media Foundation | retained output, GPU/driver identity, decode-and-compare report, synchronization/resource-registration proof, zero-readback telemetry |
| Android | Vulkan/AHardwareBuffer or GLES/EGL surface → MediaCodec | real device/API/codec identity, MP4 decode-and-compare, acquire/release sync proof, lifecycle/reset/thermal run, zero-readback telemetry |
| macOS | Metal/IOSurface/CVPixelBuffer → VideoToolbox | real Mac identity, H.264/HEVC and supported ProRes output, HDR/alpha attachment report, long-run and reset evidence |
| iOS | Metal/IOSurface/CVPixelBuffer → VideoToolbox | real iPhone identity, decoded output comparison, lifecycle/memory-pressure evidence, audio/video sync and zero-readback telemetry |

Compile-only, simulator, mock, and contract-test results are never accepted as physical qualification.

## Default release thresholds

- Maximum absolute normalized pixel error: `0.002`
- Mean absolute normalized pixel error: `0.0005`
- Maximum audio/video sync error: `20 ms`
- Minimum long-run export: `30 minutes`
- CPU readbacks: `0`
- CPU staging frames: `0`

Thresholds must be versioned if a codec, chroma-subsampling, or delivery transform requires a different documented comparison method. Lossy codec comparison must use decoded output and cannot compare encoded bytes.

## Golden scene coverage

The retained qualification suite must cover Primary Wheels, Log Wheels, RGB Curves, HSL Qualifier, masks, LUTs, correction controls, serial and parallel nodes, transforms, crop/scale, opacity/blend, SDR/HDR output transforms, alpha policy, CFR/VFR timestamps, seeking source identity, and audio sync.

## Failure and recovery coverage

Each platform report must include cancellation, resumable export, device loss or codec reset, partial-file cleanup, atomic finalization, unsupported codec/container behavior, and a long-running export. A GPU-selected job may not restart or continue on CPU. Explicit CPU/reference export remains a separate user-authorized job.

## Current source-level status

- M1 snapshot and fallback policy: implemented.
- M2 GPU export orchestrator: implemented.
- M3 Windows adapter contract: implemented; physical evidence external.
- M4 Android adapter contract: implemented; physical evidence external.
- M5 Apple adapter contract: implemented; physical evidence external.
- M6 evidence validator and matrix gate: implemented by this PR.

The repository must not be described as fully hardware-qualified until the four physical-device entries pass the validator and their artifacts are retained.

## Packaging consistency blocker

At audit time the root `CMakeLists.txt` declares version `5.50.0`, while `cmake/DigitorEngineBase.cmake` declares `5.0.0`. Release packaging must synchronize these values in a dedicated full-file-safe change before tagging a release. This PR intentionally does not partially rewrite the large reusable CMake base file.
