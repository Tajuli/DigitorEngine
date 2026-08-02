# Production Hardware Encode — v5.41.0

This milestone adds the missing GPU-frame-to-hardware-encoder production session without rewriting the existing export selector, FFmpeg runtime, timeline renderer, playback engine, or completed GPU processing subsystems.

## Runtime path

```text
existing timeline renderer / GPU node-color pipeline
  -> ProcessedGpuFrame
  -> ProductionHardwareEncodeSession
  -> platform encoder adapter
  -> drain
  -> atomic output finalization
```

## Enforced contracts

- A production session may require a hardware backend; software fallback is not silently selected.
- Input must be a ready, live, GPU-resident `ProcessedGpuFrame`.
- CPU frames are rejected and counted.
- The session never invokes validation readback or converts frames through an RGBA CPU buffer.
- Frame dimensions must match the export profile.
- PTS values are monotonic when requested.
- Encoder open, GPU-frame submission, drain, cancellation, and atomic finalization are explicit backend callbacks.
- Progress and failure provenance are observable through telemetry.
- Destruction of a running session invokes cancellation rather than abandoning the encoder.

## Platform adapters

The host supplies a backend adapter using the callback contract:

- Windows: NVENC, Intel Quick Sync, or AMD AMF as available.
- macOS/iOS: VideoToolbox.
- Android: MediaCodec.

The adapter owns platform-specific texture/surface interoperability, encoder configuration, packet output, audio mux integration, and temporary-file rename semantics. The engine coordinator owns cross-platform state, validation, timestamp ordering, and no-silent-fallback behavior.

## Validation boundary

The deterministic host test verifies open, three GPU-frame submissions, zero CPU readbacks, monotonic timestamp rejection, strict hardware policy, drain, atomic finalization, progress, and cancellation. Real codec output, 8/10-bit pixel formats, HDR metadata, driver interoperability, and long-duration exports remain physical-device qualification gates.
