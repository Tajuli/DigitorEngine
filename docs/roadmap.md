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
