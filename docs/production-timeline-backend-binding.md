# Production timeline GPU backend binding

The professional timeline model, editing operations, execution plans, media adapter,
GPU-resident runtime, cache, playback and export orchestration already exist. This
binding closes the remaining source-level assembly gap without creating a second
timeline or renderer.

## Authoritative path

```text
TimelineRenderExecutor
  -> TimelineMediaAdapter
  -> ProductionTimelineGpuBinding
  -> selected backend create-target callback
  -> selected backend node/effect dispatch
  -> selected backend layer compositor
  -> ProcessedGpuFrame
  -> preview or production hardware export
```

The binding is backend-neutral and works with Vulkan, D3D12, Metal or OpenGL ES
hosts. Native resources remain private to each backend and are never exposed by
the stable C ABI.

## Fail-closed requirements

A production binding is invalid unless it provides:

- a non-CPU selected renderer backend;
- an opaque context/device identity;
- RGBA16F working precision;
- a stable device identity;
- GPU target creation;
- native effect/node dispatch;
- native compositing;
- a completion/fence-based cache-eviction query.

Every frame is rejected unless backend, context, readiness, dimensions, timestamp
and working format match. CPU or mixed CPU/GPU frames are rejected. The binding
does not perform readback, CPU staging or silent fallback.

## Reused systems

- professional multitrack timeline and editing core;
- deterministic preview/export execution plan;
- real-media adapter and strict native-surface decode/import;
- ProductionNodeGraph and backend-native node operations;
- ProcessedGpuFrame lifetime and context retirement;
- GPU cache and memory-pressure policy;
- production playback and export orchestrators.

## Qualification boundary

The source-level timeline assembly is complete when its contract tests and normal
CI pass. Production release still requires the existing real-device matrix to run
the host callbacks on D3D12, Vulkan, Metal and GLES hardware, including layered
compositing, transitions, transforms, node/color operations, device loss, memory
pressure and preview/export parity. Compile-only or mock execution is not hardware
evidence.
