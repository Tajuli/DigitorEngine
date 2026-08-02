# Production platform integration bundle

This change assembles the already-implemented timeline, native preview, hardware-encode and zero-copy contracts through one authoritative platform factory. It does not create a second renderer, timeline, texture queue, encoder session or media pipeline.

## Shared device contract

A production assembly is accepted only when the timeline backend, Flutter texture host and frozen export snapshot use the same selected backend and device/context identity.

- Windows: Vulkan or D3D12
- Android: Vulkan or OpenGL ES
- macOS/iOS: Metal

CPU frames, mixed CPU/GPU frames, retired contexts, backend mismatches and hidden software fallback remain explicit failures.

## Flutter texture hosts

`ConcreteFlutterTextureHost` implements the existing `NativePreviewTextureHost` interface for the four platform families. The Flutter embedding supplies its real registrar callback and retains the exact `ProcessedGpuFrame` until the generation is consumed. The engine performs no pixel mapping, RGBA byte-buffer conversion or readback.

Platform embeddings must bind the callback to their native Flutter texture registrar:

- Windows: D3D12/Vulkan external texture or backing texture registration
- Android: AHardwareBuffer/EGL/Vulkan-backed texture registration
- macOS/iOS: IOSurface/CVPixelBuffer/Metal-backed texture registration

## Encoder factory assembly

The platform factory selects and owns the existing strict adapters:

- Windows: NVENC, Media Foundation or Quick Sync adapter
- Android: MediaCodec adapter
- macOS/iOS: VideoToolbox adapter

The frozen snapshot, encoder callbacks and zero-copy qualification function are returned as one assembly. No FFmpeg file re-decode path or software retry is introduced.

## Windows Vulkan interop

Windows Vulkan export is accepted only when the platform reports real DXGI external-memory import/export, external-semaphore synchronization, NV12 and P010 support, matching adapter identity and zero CPU staging/readback. The platform callback converts/exports the final Vulkan GPU frame to the existing D3D12 encoder-resource path. Missing interop fails closed; it does not silently switch to D3D12 or CPU mid-session.

## Canonical version

`cmake/DigitorEngineVersion.cmake` is the single version source. The top-level build restores canonical project variables after including the legacy base CMake file and uses the same value for the target version, package-version file and SOVERSION.

## Qualification boundary

The source-level factories and fail-closed contracts are included in the existing production hardware encode test. A release still requires physical Windows, Android, macOS and iOS evidence for the actual platform registrar callbacks, native encoder sessions, Vulkan/DXGI interop, output decode-and-compare, long-run stability and zero-copy telemetry. Contract tests cannot replace those device runs.
