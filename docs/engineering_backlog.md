# Prioritized engineering backlog

This backlog converts current simulations/placeholders into production implementations. It adds
no implementation in this audit; ordering reflects claim risk and dependency structure.

## P0 — Stop false-positive capabilities and establish executable foundations

1. **Native command contract:** redesign `IRenderBackend` around backend-owned queues, native
   command recording/submission, fences, resource states, upload/readback, and failure/device-loss
   semantics. Keep CPU callbacks explicitly named as reference execution.
2. **Backend execution:** implement Vulkan queue families/command pools/buffers, D3D12
   queues/allocators/lists, Metal command queues/encoders, and GLES context/surface ownership plus
   draw/compute strategy. Add native synchronization and transitions.
3. **Real shaders/pipelines:** integrate real GLSL→SPIR-V/HLSL/MSL toolchains as appropriate;
   reflection validation, descriptor/root/argument binding layouts, native pipelines, cache
   persistence/versioning, and dispatch/draw.
4. **Readback and validation:** add texture upload/copy/readback and backend pixel tests with native
   validation/debug layers. This blocks all GPU and parity claims.
5. **Real media decode:** implement FFmpeg open/probe/demux/packet/decode/seek/flush, timestamp
   conversion, swscale/swresample, errors, and bounded decoded-frame caching. Remove metadata-only
   hardware claims until real DXVA/VideoToolbox/MediaCodec integration is selected successfully.
6. **Real encode/mux:** add codec/container/image encoders, mux finalization, audio, metadata, and
   independent conformance/playback tests. Never route standard format enums to private output.
7. **C exception boundary/concurrency:** make every exported function `noexcept` in behavior,
   provide uniform error translation, and prevent concurrent handle check/destroy use-after-free
   with a specified ownership/concurrency model.

## P1 — Make shared rendering and editing truthful

8. **Decoded-frame render input:** make immutable decoded source identity/PTS and graph revision
   explicit inputs to `SharedRenderer`; connect Timeline scheduling to decode and composition.
9. **Shared native graph:** make preview and export invoke the same compiled native graph over the
   same source-frame representation, with well-defined display-only transforms.
10. **Preview surfaces:** implement swapchain/layer/surface bridges for Win32, Android, macOS, and
    iOS, including lifecycle, resizing, color management, pacing, and device loss.
11. **GPU/encoder path:** implement synchronized GPU-frame interop where supported and explicit
    readback/conversion otherwise; test backpressure and cancellation.
12. **Timeline render/audio:** implement source time-base mapping, VFR/frame selection, clip
    compositing, transitions, audio decode/resample/mix/sync, and deterministic export scheduling.
13. **Correct caching:** version/cache all material inputs including `NodeContext::values`, source
    and effect state, graph/timeline generation, dimensions/formats, and color metadata; add
    invalidation and concurrency tests.
14. **Node execution:** compile node shader metadata into real native passes, support safe parallel
    branches, validate frame shapes/formats, and serialize all deterministic state or explicitly
    version external resolver state.

## P1 — Color/effects correctness

15. **Color specification:** define working space, transfer/range, chromatic adaptation,
    premultiplication, precision, operation ordering, clamping, NaN/Inf, and metadata propagation.
16. **Complete controls:** implement specified per-channel lift/gamma/gain/offset, accurate
    temperature/tint, curves, and HSL qualifier; validate existing exposure/contrast/saturation/
    vibrance/hue behavior against an independent oracle.
17. **Native color/LUT/effects:** implement actual shaders for every operation and interpolation
    mode; benchmark without changing mathematical behavior; prove CPU/GPU error budgets on real
    backends. Expand `.cube` parsing/error/domain/order conformance coverage.

## P2 — Platform and release qualification

18. **Target builds/packages:** create reproducible Windows, Android NDK/package, macOS, and iOS
    toolchain/build outputs plus separate consumer tests. Do not count Apple conditionals as iOS.
19. **Hardware decode matrices:** integrate and validate DXVA, VideoToolbox, and MediaCodec with
    zero-copy paths where possible and forced software fallback everywhere.
20. **Device lab/CI:** run representative vendor/API/OS matrices with GPU captures, validation
    layers, media fixtures, surface tests, sanitizers, and retained reports.
21. **ABI program:** publish calling/ownership/threading rules, ABI baselines and compatibility
    tests, feature negotiation, shared/static symbol policies, and semantic versioning criteria.
22. **Release gate:** execute every item in `docs/validation_plan.md`; publish limitations and
    evidence before restoring GPU-first, cross-platform, stable-ABI, real-media, or pixel-identical
    wording.
