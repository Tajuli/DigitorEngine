# DigitorEngine

DigitorEngine is a C++20, GPU-first rendering and media-engine foundation for the Digitor cross-platform video editor.

Current package version: **0.0.1**.

The repository contains substantial source implementations for rendering, color processing, node execution, timeline editing, media decode, playback, export orchestration, audio/video synchronization, and Flutter-facing runtime integration. It is not yet claimed as universally production-qualified on all Windows, Android, macOS, and iOS hardware. Real-device performance, driver interoperability, native texture registration, and long-duration stress validation remain release gates.

## Current architecture

The intended production path is:

```text
Media source
  -> hardware or software decoder
  -> native media surface / decoded frame
  -> GPU import
  -> timeline and node/color processing
  -> production playback scheduler
  -> native platform presenter / Flutter texture
  -> shared production export path
```

### Production execution policy

DigitorEngine is GPU-first, not GPU-only. Production backend selection must prefer a supported GPU backend. A multithreaded CPU implementation is the fallback only when no usable GPU backend can be selected or initialized for the device. Once a GPU backend has been selected, a later GPU execution failure must be surfaced as an error and must not silently switch the active job to CPU.

Color processing is required to be deterministic and per-pixel accurate for the same input, parameters, color-space state, and render mode. Preview and export must use equivalent color math and are release-qualified with numerical/per-pixel parity checks. A single-thread CPU fallback or a preview/export color mismatch is not release-qualified behavior.

## Implemented subsystems

### Core rendering

- C++20 engine and C-compatible opaque-handle API
- CPU reference backend
- Vulkan, Direct3D 12, Metal, and OpenGL ES backend infrastructure
- textures, buffers, samplers, command recording, render graph, shader compilation/reflection contracts, and pipeline-cache infrastructure
- deterministic validation and backend qualification helpers

### Color and image processing

- correction controls including exposure, contrast, saturation, temperature, tint, highlights, shadows, hue, and color boost
- Primary Wheels
- Log Wheels
- RGB Curves
- HSL Qualifier
- 1D and 3D LUT support
- blur, sharpen, glow, grain, vignette, motion blur, masks/windows, and related effect infrastructure
- CPU reference implementations plus native GPU execution paths and qualification contracts where implemented

### Node system

- serial nodes
- parallel branches
- mixer and output nodes
- production node graph and native node execution runtime
- shader/kernel contracts and backend runtime factories
- deterministic graph and qualification tests

### Timeline and editing

- multitrack timeline
- professional timeline editing suite
- clip and track operations
- track enablement and removal
- timeline completion APIs
- timeline render execution and runtime
- media adapter integration
- preview/export frame-selection foundations

### Playback

- playback transport
- audio-master synchronization and latency compensation
- play, pause, stop, seek, scrub, frame step, looping, forward and reverse rates
- background decode-ahead worker
- bounded GPU-frame queue and memory budget
- stale-frame rejection after seek
- deadline-aware hold/drop behavior
- adaptive full, half, quarter, and proxy quality
- thermal and memory-pressure controls
- playback telemetry

### Media decode

- FFmpeg-based media opening and packet decoding when FFmpeg is available
- hardware decoder selection contracts for D3D11VA, VideoToolbox, and MediaCodec
- production hardware-decode coordinator
- native-surface and zero-copy importer contracts
- strict CPU-frame rejection in zero-copy production sessions
- timestamp and qualification evidence checks

The generic FFmpeg `VideoDecoder` may still transfer some hardware frames into CPU-accessible memory for its CPU-frame API. The production zero-copy path is provided through `ProductionHardwareDecodeSession` with backend-specific native importers supplied by the platform host.

### Unified real-media Flutter runtime

v0.0.1 includes `UnifiedRealMediaRuntime`, which reuses the existing production components rather than rewriting them:

```text
timeline timestamp
  -> frame resolver
  -> ProductionHardwareDecodeSession
  -> optional existing GPU timeline/node/color pipeline
  -> ProductionPlaybackEngine
  -> native Flutter platform presenter
```

The presenter receives a `ProcessedGpuFrame` plus backend, format, dimensions, timestamp, frame identity, and generation metadata. This coordinator performs no RGBA byte-buffer conversion or CPU texture readback.

Platform hosts remain responsible for the final native bridge:

- Windows: D3D11VA import and D3D12/Flutter texture presentation
- Android: MediaCodec/AHardwareBuffer import and Vulkan or OpenGL ES presentation
- Apple: VideoToolbox/CVPixelBuffer import and Metal presentation

### Export

- production export orchestration
- FFmpeg export runtime
- hardware-export runtime contracts
- asynchronous export jobs
- progress, cancellation, and error reporting
- resumable segment export
- timeline render integration
- MP4, MOV, Matroska, image-sequence, and supported codec paths when the required FFmpeg components are present

Not every public export entry point is guaranteed to use a zero-copy hardware encoder on every platform. Hardware encode and physical-device interoperability must be qualified per backend and codec.

## Build

### Basic host build

```bash
cmake -S . -B build \
  -DDIGITOR_BUILD_TESTS=ON \
  -DDIGITOR_BUILD_EXAMPLES=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Require FFmpeg

```bash
cmake -S . -B build \
  -DDIGITOR_ENABLE_FFMPEG=ON \
  -DDIGITOR_REQUIRE_FFMPEG=ON \
  -DDIGITOR_BUILD_TESTS=ON
```

FFmpeg discovery supports pkg-config and `DIGITOR_FFMPEG_ROOT`. The engine does not download or vendor FFmpeg binaries. Deployments are responsible for shipping compatible runtime libraries and complying with FFmpeg licensing requirements.

### Install and consume

```bash
cmake --install build --config Release --prefix install
```

A CMake consumer can then use:

```cmake
find_package(DigitorEngine CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE Digitor::Engine)
```

The installed package also resolves the required thread dependency.

## Tests

The CMake test suite includes dedicated targets for:

- core engine and editor behavior
- color processing and native GPU paths
- node graph and node execution
- audio latency and synchronization
- playback transport and production playback
- unified real-media runtime
- multitrack and professional timeline editing
- production export, FFmpeg export, hardware-export contracts, and export jobs
- timeline render execution/runtime/media adapter
- production hardware decode

FFmpeg real-media and VFR parity tests are enabled only when FFmpeg is found and the corresponding fixture path is supplied.

## Platform status

| Platform | Engine/backend source | Production decode/import path | Native Flutter presentation | Current qualification boundary |
|---|---|---|---|---|
| Windows | D3D12 and optional Vulkan | D3D11VA/native-surface contracts | Host adapter required | Real GPU/driver/device tests required |
| Android | Vulkan and OpenGL ES | MediaCodec/AHardwareBuffer contracts | Host adapter required | Real Adreno/Mali device tests required |
| macOS | Metal | VideoToolbox/CVPixelBuffer contracts | Host adapter required | Apple Silicon and Intel device tests required where supported |
| iOS | Metal | VideoToolbox/CVPixelBuffer contracts | Host adapter required | Real iPhone/iPad tests required |

Linux is primarily used for portable CPU builds, tests, FFmpeg validation, and CI. It is not currently a primary Digitor product target.

## Production-readiness statement

Source-level implementation and contract tests do not by themselves prove device-level production readiness. Before release, each target requires:

- physical-device hardware decode, GPU import, render, presentation, and encode validation
- codec and pixel-format matrix testing, including 8-bit/10-bit and SDR/HDR paths as applicable
- preview/export identity and numerical comparison
- device-loss, app suspend/resume, resize/orientation, and memory-pressure recovery
- long playback and export stress testing
- thermal, latency, dropped-frame, and memory benchmarks
- Flutter plugin texture-registration and lifecycle validation
- proof that CPU fallback is multithreaded and occurs only when no usable GPU backend can be selected
- per-pixel color parity evidence between preview and export for qualified color operations

No README claim should be interpreted as evidence that every backend is already qualified on every device.

## Documentation

Useful source-backed documents include:

- `docs/unified_real_media_runtime.md`
- `docs/production_readiness.md`
- `docs/implementation_status.md`
- `docs/platform_support.md`
- `docs/native_gpu_pipeline.md`
- `docs/deterministic_rendering.md`
- `docs/rgb_curves.md`
- `docs/color_science.md`
- `docs/roadmap.md`

Some older milestone documents are historical records. The current source tree, CMake targets, public headers, tests, and this README take precedence for v0.0.1 behavior.

## Public API and compatibility

Public headers are installed from `include/digitor`. The repository exposes C and C++ APIs for different subsystems. Consumers should pin a tested engine version; long-term binary compatibility across all historical releases is not implied solely by the current major version.

## License

MIT License
