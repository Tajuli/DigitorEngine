# DigitorEngine

DigitorEngine is an **experimental C++20 rendering-engine foundation**. The repository reports
version **3.0.0**, but that number is not evidence of production readiness or ABI stability.
The implementation has a CPU reference path, native GPU resource allocation on selected
platforms, editing data structures, and deterministic graph/LUT/effect prototypes. It does not
contains the first native preview passes on Metal and OpenGL ES, but does not yet
contain production media I/O or qualified Vulkan/D3D12 graphics pipelines.

## Audited status

The legacy portable command layer executes recorded C++ callbacks synchronously on the CPU.
Vulkan, Direct3D 12, Metal, and OpenGL ES backends allocate some native resources and perform
device discovery, but do not create native command queues/buffers, compile native shaders,
create pipelines/descriptors, dispatch/draw, transition textures, synchronize GPU work, or
read rendered pixels back. The v3 preview path now records a Metal clear pass and a GLES
fullscreen texture-copy shader and returns GPU-completed pixels to preview. See the
[native pipeline notes](docs/native_gpu_pipeline.md) for exact backend scope.

FFmpeg libraries can be detected and linked, but the decoder implementations return empty,
timestamped placeholder frames. Export writes a private `DIGITOR` text/interchange stream or
raw `.rgba` float buffers; it does not encode or mux MP4, MOV, MKV, or standard images. Preview
is an in-memory cached `VideoFrame`, not a native display surface. Preview and export both call
the same CPU-oriented `SharedRenderer`, but no test proves they consume the same decoded frame,
execute a native shader graph, or produce pixel-identical results.

See the evidence-based [implementation audit](docs/implementation_status.md), honest
[platform matrix](docs/platform_support.md), [validation plan](docs/validation_plan.md), and
[engineering backlog](docs/engineering_backlog.md).

## What is currently usable

- CMake static/shared-library build configuration and a C-compatible opaque-handle API.
- CPU-backed texture/buffer storage and native resource allocation prototypes.
- CPU color operations, `.cube` LUT parsing/interpolation, and CPU image effects.
- A synchronous callback command model and dependency-ordered render graph prototype.
- A CPU callback node graph with deterministic serialization and a limited frame cache.
- Timeline edit data structures (tracks, clips, edits, keyframes, undo/redo).
- Unit tests for the CPU/reference and data-structure paths on the host build.

These components remain experimental. The C API lacks a documented ABI compatibility policy;
not every exported function is protected against C++ exceptions; handle operations are not
safe against concurrent destroy/use; and no packaged mobile or desktop application is supplied.

## Platform status

| Platform | Build configuration | Native resource code | Native rendering | Media/preview/export validation |
|---|---|---|---|---|
| Windows | Configured, not verified in this audit | Vulkan optional; D3D12 present | Not implemented | Not verified |
| Android | Configured, not verified in this audit | Vulkan optional; GLES requires a current EGL context | Not implemented | Not verified |
| macOS | Configured, not verified in this audit | Metal present | Not implemented | Not verified |
| iOS | Conditional Apple source only; no iOS toolchain/project | Metal source present | Not implemented | Not verified |

Linux is useful for the CPU reference build but is not one of the claimed production targets.

## Build the audited host configuration

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

FFmpeg detection does not enable decoding or encoding; it only changes
`ffmpeg_available()` in the current implementation.

## Public API

The C header is `include/digitor/digitor.h`. Treat it as experimental rather than ABI-stable
until the ABI and concurrency work in the backlog is complete.

## Roadmap

See [docs/roadmap.md](docs/roadmap.md). Items are now organized by demonstrated capability,
not by the repository version number.

## License

MIT License
