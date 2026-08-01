# Production Real-Time Playback Engine — v5.39.0

This milestone turns the existing playback transport, live audio synchronization, multitrack timeline execution and hardware-decode path into one bounded real-time playback runtime.

## Runtime path

`timeline position -> decode-ahead worker -> GPU frame queue -> deadline/drop selection -> platform presenter`

Normal playback requires `ProcessedGpuFrame` objects. A CPU renderer frame is rejected when `require_gpu_frames` is enabled; the runtime never performs pixel readback or silently substitutes a software frame.

## Completed capabilities

- C++20 background decode-ahead worker with stop-token shutdown.
- Bounded frame count and GPU-memory budget.
- Audio-master transport integration and bounded drift correction reuse.
- Latest-seek-wins generation invalidation and stale-frame rejection.
- Forward and reverse shuttle rates from 0.25x through 4x.
- Frame stepping and low-latency scrubbing.
- In/out loop playback.
- Deadline-aware hold/drop behavior.
- Adaptive full, half, quarter and proxy playback quality.
- Memory-pressure and thermal-pressure quality response.
- Strict GPU-frame policy and CPU-frame rejection telemetry.
- Decode, presentation, queue, memory, seek, stale-frame, drop and latency telemetry.
- Platform-independent decode and present callbacks so D3D12, Vulkan, Metal and GLES consumers use the same scheduler.

## Safety contracts

- Queue size and memory usage remain bounded.
- A seek invalidates all frames from the previous generation.
- Decoder and presenter errors remain explicit.
- Import or presentation failure does not activate CPU fallback.
- Stop clears queued GPU ownership before resetting transport.
- Destruction requests worker cancellation and wakes blocked waits.

## Qualification boundary

The deterministic host test validates scheduling, seek coalescing, adaptive proxy selection, transport controls, queue cleanup and CPU-frame rejection. Final performance claims still require the planned physical-device matrix using real D3D11VA/MediaCodec/VideoToolbox decode surfaces and real swapchain/Metal/GLES presentation.
