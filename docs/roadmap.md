# Roadmap

This roadmap describes remaining engineering work. “Present” means source exists; it does not
mean production-verified. Historical milestone labels and the current `2.0.0` version must not
be interpreted as completion claims.

## Present foundations

- Engine/context lifecycle, backend selection, opaque C handles, and CPU allocations.
- Native texture, buffer, and sampler allocation prototypes for conditional platform backends.
- CPU callback command/render graph, CPU color/effects/LUT implementations, CPU callback node
  graph, timeline editing model, frame caches, and preview/export orchestration prototypes.
- Host unit tests covering reference math, data structures, and API lifecycle.

## Required before a GPU-rendering claim

- Backend-owned native queues, command pools/buffers/encoders, fences/semaphores, and resource
  state tracking for Vulkan, D3D12, Metal, and GLES.
- Real shader toolchains, reflection, layouts/bindings, native pipelines, draw/dispatch,
  upload/download, presentation, validation-layer-clean execution, and device-loss handling.
- Backend pixel tests against an independent CPU oracle on actual supported hardware.

## Required before media and shared-rendering claims

- **Completed:** FFmpeg demux, packet iteration, video/audio software decoding, timestamp conversion,
  pixel/sample conversion, seek/flush, and software fallback. Hardware-frame integration remains.
- A render request sourced from the same decoded frame and timeline composition for both preview
  and export; real platform preview surfaces; GPU-to-encoder interop or explicit readback.
- Standards-compliant image encoding and MP4/MOV/MKV codec encoding/muxing, verified by
  independent probes and playback.
- Numerical preview/export identity tests, including color metadata and deterministic effects.

## Required before editing/color claims

- Specify the working color space, transfer functions, range, chromatic adaptation, alpha, and
  precision policy; implement curves and HSL qualification; validate LUT ordering/domains.
- Execute color, LUT, effects, and node graphs in native shaders and prove CPU/GPU tolerances.
- Connect timeline clips to decoded video/audio, compositing, transitions, frame-rate/time-base
  conversion, audio synchronization/mixing, and dependency-aware cache invalidation.

## Required before cross-platform/production/ABI claims

- Reproducible Windows, Android, macOS, and iOS builds plus device test artifacts and reports.
- Host preview bridges and mobile lifecycle/context handling; decode and encode matrices on each
  platform; GPU validation/debug-layer runs on representative vendors and OS versions.
- Catch all exceptions at the C boundary, define handle concurrency and ownership, add symbol
  visibility/export tests for static and shared builds, and publish an ABI/versioning policy.
- Complete the gates in `docs/validation_plan.md` and close `docs/engineering_backlog.md`.
