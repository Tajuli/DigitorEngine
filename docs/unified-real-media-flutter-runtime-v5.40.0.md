# Unified Real-Media Flutter Runtime — v5.40.0

This milestone adds only the missing integration layer. Existing hardware decode, zero-copy import, GPU node/color processing, production playback, timeline, and export subsystems are not rewritten.

## Runtime path

`timeline timestamp -> existing frame resolver -> ProductionHardwareDecodeSession -> optional existing GPU timeline/node/color pipeline -> ProductionPlaybackEngine -> native Flutter platform presenter`

## Contracts

- Hardware decode remains strict and zero-copy.
- CPU-resident frames are rejected.
- Existing GPU processing is injected through `ExistingGpuPipeline` rather than duplicated.
- The native Flutter presenter receives the existing `ProcessedGpuFrame` plus backend, format, dimensions, timestamp, identity, and generation metadata.
- No RGBA byte-buffer conversion or CPU texture readback is performed by this runtime.
- Seek and scrub flush the existing decoder before updating playback state.
- Audio-master clock, adaptive quality, memory pressure, thermal pressure, reverse playback, frame stepping, and telemetry remain owned by the existing production playback engine.
- Native surface ownership remains inside the backend-created `ProcessedGpuFrame`; the coordinator does not create a second ownership path.

## Host responsibilities

The platform host supplies the already-implemented backend-specific native importer and presenter:

- Windows: D3D11VA surface import and D3D12/Flutter texture presentation
- Android: MediaCodec/AHardwareBuffer import and Vulkan or GLES presentation
- Apple: VideoToolbox/CVPixelBuffer import and Metal presentation

Physical-device performance and driver qualification remain a release gate, not a source-level fallback.
