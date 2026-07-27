# DigitorEngine

DigitorEngine is an **experimental C++20 rendering-engine foundation**. The repository reports
version **4.6.1**, but that number is not evidence of production readiness or ABI stability.
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

When FFmpeg libraries are available, the media API performs packet-based software decoding into
real RGBA pixels and float PCM. Export writes a private `DIGITOR` text/interchange stream or
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

FFmpeg detection enables software decoding; encoding remains outside this milestone.

## Public API

The C header is `include/digitor/digitor.h`. Treat it as experimental rather than ABI-stable
until the ABI and concurrency work in the backlog is complete.

## Roadmap

See [docs/roadmap.md](docs/roadmap.md). Items are now organized by demonstrated capability,
not by the repository version number.

## License

MIT License

## FFmpeg software media decoding

When `libavformat`, `libavcodec`, `libavutil`, `libswscale`, and `libswresample` are found by
pkg-config, DigitorEngine opens standard FFmpeg-supported containers (including MP4, MOV, MKV,
and common audio containers), selects the best stream, and performs software packet decoding.
Video is normalized to top-down RGBA32F and audio to interleaved float PCM. All PTS and duration
fields use a 1/1,000,000-second engine timebase. Random backward access flushes and resets the
codec; `seek(pts_us)` provides timestamp seeking. Configure with `-DDIGITOR_REQUIRE_FFMPEG=ON`
to reject a build without the five development libraries. Hardware decoding and encoding remain
out of scope.


## 4.x render, export, and Flutter SDK

Preview and export now consume one `SharedRenderer` render graph. Pixel regression helpers expose PSNR and SSIM and validate the exact pre-encode pixels. FFmpeg-backed exports support MP4, MOV, Matroska, PNG/TIFF/EXR sequences and H.264, H.265, or AV1 video, with encoder draining, muxing, cancellation, and progress callbacks. The C ABI exposes a non-blocking Flutter SDK session and native RGBA texture bridge for Windows, Android, iOS, and macOS; see `flutter/digitor_sdk/example`. AAC is selected through `ExportSettings::audio_codec` when an audio source is attached.

## Stabilization build and FFmpeg dependency policy

All desktop configurations are exercised with tests/examples enabled and FFmpeg disabled; dedicated jobs
require FFmpeg. `DIGITOR_WARNINGS_AS_ERRORS=ON` applies strictness only to engine-owned compilation.
Install verification uses `cmake --install` followed by pure C and C++ projects using the exported package.

FFmpeg discovery first accepts pkg-config and also accepts `-DDIGITOR_FFMPEG_ROOT=/sdk/ffmpeg`. Linux users
install their distribution's `libavcodec`, `libavformat`, `libavutil`, `libswscale`, and `libswresample`
development packages. Homebrew `ffmpeg` plus `pkg-config` is supported on macOS. A Windows SDK root must
contain `include/libavcodec/avcodec.h` and `lib` (or `lib64`) import/static libraries named `avcodec`,
`avformat`, `avutil`, `swscale`, and `swresample` (optional `lib` prefix). Shared deployments must copy the
matching FFmpeg runtime DLLs beside the application. On macOS deploy matching dylibs with corrected
`@rpath` install names or require the Homebrew runtime. Configuration never downloads or vendors binaries.
The summary says “enabled and linked”, “unavailable”, or fails when `DIGITOR_REQUIRE_FFMPEG=ON`.

Engine compilation requires only the five development libraries, exposed to CMake as
`DIGITOR_FFMPEG_LIBRARIES`; it never requires the `ffmpeg` command-line program. The independently
discovered `DIGITOR_FFMPEG_CLI` is optional. Set `-DDIGITOR_GENERATE_TEST_MEDIA=ON` to require that
program and generate the optional MP4/MOV/MKV/WAV fixture set. The option defaults to `OFF`; without
the CLI, media tests continue with repository-owned Y4M/WAV source fixtures and malformed input.

Generated media fixtures are created by `scripts/generate_media_fixtures.sh` from FFmpeg lavfi sources;
MP4/H.264, MOV, MKV, WAV, and malformed inputs are never opaque checked-in binaries. Determinism and future
plugin design are specified in [deterministic rendering](docs/deterministic_rendering.md) and
[plugin architecture](docs/plugin_architecture.md). Current qualification limits are in
[production readiness](docs/production_readiness.md); long-term ABI stability is not claimed.

## Native shader milestone
The canonical source contract is HLSL through DXC. Vulkan output must pass SPIRV-Tools validation and binary reflection before it is usable. Missing toolchains and unfinished D3D12/Metal/GLES reflection paths fail explicitly; the engine never substitutes a CPU callback after native compilation failure. See [the compiler](docs/shader_compiler.md), [reflection](docs/shader_reflection.md), [ABI](docs/shader_abi.md), and [cache](docs/pipeline_cache.md) documentation.

## Color science v4.5

The public C++ color-science foundation defines explicit metadata, linear BT.709
working space, standards-based transfer functions, derived primary matrices,
Bradford adaptation, integer YUV decoding, immutable transform graphs and
baseline tone operators. See `docs/color_science.md` and the honest backend truth
table in `docs/implementation_status.md`. Native color-graph GPU execution is not
yet implemented; v4.5.0 does not claim ACES or a complete HDR pipeline.

## v4.6.1 `grade_rgba32f` execution qualification

The engine now records internal execution provenance for native grading and
has deterministic no-fallback failure seams. The complete CPU/Vulkan/D3D12/
Metal/GLES call graphs and evidence truth table are in
[`docs/grade_rgba32f_execution.md`](docs/grade_rgba32f_execution.md). The native
FP32 implementations issue real commands, but this audit did not run qualifying
GPU hardware, so every GPU backend remains **Implemented,
hardware-unverified**. FP16 is unsupported. Version 4.6.1 is an audit milestone,
not a production-readiness claim.

### RGB Curves

An immutable FP32 CPU reference, monotone-cubic control-point compiler, deterministic 256/1024/4096-sample LUTs, and explicit CPU Render Graph node are documented in [`docs/rgb_curves.md`](docs/rgb_curves.md). Native GPU curves are truthfully unsupported; there is no CPU fallback after GPU selection.
