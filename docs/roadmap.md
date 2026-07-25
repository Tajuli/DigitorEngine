# Roadmap

## v0.1.0 — Foundation

- CMake
- Stable C API
- Engine lifecycle
- Render context
- Backend abstraction
- CPU fallback
- Tests and CI

## v0.2.0 — GPU device layer

- [x] Vulkan device discovery (Windows and Android)
- [x] Metal device discovery (macOS and iOS)
- [x] D3D12 device discovery fallback (Windows)
- [x] OpenGL ES discovery fallback (Android)
- [x] Platform backend priority and CPU fallback
- [x] GPU capability reporting

Rendering, command queues, and shader execution begin in later milestones; v0.2.0 performs discovery only.

## v0.3.0 — Resource layer

- Textures
- Buffers
- Samplers
- Framebuffers
- Resource lifetime tracking
- Upload/download staging

## v0.4.0 — Shader pipeline

- Shader modules
- Pipeline cache
- Unified shader metadata
- Preview/export shared shader graph
- Exposure and passthrough shaders

## v0.5.0 — Color engine

- Linear-light working space
- Lift/gamma/gain/offset
- RGB curves
- HSL qualifier
- LUT support
- CPU reference implementation

## v0.6.0 — Media I/O

- FFmpeg integration
- Decode
- Encode
- Frame timestamps
- Pixel-format conversion

## v0.7.0 — Timeline

- Tracks
- Clips
- Transitions
- Frame scheduling
- Cache

## v0.8.0 — Host integration

- Flutter FFI
- Windows texture bridge
- Android texture bridge
- iOS/macOS texture bridge

## v1.0.0

- Shared preview/export rendering path
- GPU-first with CPU fallback
- Cross-platform production API

## v0.9.0–v1.1.0 — Editing pipeline (complete)

- FFmpeg feature detection, video/audio decoder contracts, platform hardware selection
  (DXVA, VideoToolbox, and MediaCodec), CPU fallback, and bounded LRU frame caches.
- Frame-number-based timeline tracks with overwrite/insert, ripple, roll, slip, slide,
  keyframe interpolation, undo, and redo.
- Cached preview with zoom, pan, and transform state. Preview and export both invoke the
  same `SharedRenderer` render graph, ensuring frame-identical graph execution.
- MP4, MOV, MKV, and image-sequence export targets. Container encoding remains delegated
  to FFmpeg-enabled integrations; the portable target emits a deterministic interchange stream.

## v0.4.0 — Native GPU Resource Layer (complete)
- Backend-neutral validated textures, buffers, upload/staging memory, and samplers.
- Vulkan resources are optional at build time; D3D12 is Windows-only, Metal is Apple-only Objective-C++, and GLES is Android-only.
- CPU fallback remains available without GPU SDKs.
- No shaders, rendering passes, command submission, decoding/encoding, timeline, preview, or export are part of this milestone.

## v0.5.0–v0.8.0 — Compute foundations (complete)

- Backend-neutral command recording, synchronization, barriers, and triple-buffered frame contexts.
- GLSL, HLSL, MSL and SPIR-V validation/reflection with deterministic shader and pipeline caches.
- Compiled render graphs with dependencies, transient aliasing and barrier scheduling.
- Linear 32-bit floating-point CPU/GPU-command color grading with exposure, contrast, gamma, lift, gain, offset, temperature, tint, saturation, vibrance and hue.
- Rendering and timeline functionality remain out of scope.
