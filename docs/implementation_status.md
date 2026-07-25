# Implementation status audit

**Audit baseline:** v2 audit commit `4ae03d4`, formerly reporting `2.0.0`.
The v3 scoped native-preview changes are documented in `docs/native_gpu_pipeline.md`.
This is a source audit plus a Linux CPU build/test run; no Windows, Android, Apple, or
native GPU device was available. Status vocabulary is limited to the requested five labels.

## Executive conclusion

The engine is not currently GPU-first in execution. Its portable `CommandBuffer` stores
`std::function<void()>` callbacks and `CommandQueue::submit` invokes them in a CPU loop
(`include/digitor/commands.hpp:20-23`, `src/gpu/commands.cpp:10-16`). Native backends allocate
resources, but their interface exposes no native recording, pipeline, binding, dispatch, draw,
presentation, transition, or readback methods (`src/gpu/gpu_backend.hpp:12-27`). Accordingly,
every color/LUT/effect function named `*_gpu` is CPU simulation/reference only.

FFmpeg detection is real CMake feature detection (`CMakeLists.txt:35-44`), but media classes do
not call FFmpeg and return empty frames (`src/media/media.cpp:23-35`). Export is not encoding:
it writes a private text header/records or raw `Color` bytes (`src/render/renderer.cpp:11`).

## 1. GPU execution

| Claim/capability | Status | Evidence and finding |
|---|---|---|
| Portable command queue/buffer | CPU simulation/reference only | Recorded objects are CPU lambdas, barriers merely validate IDs, submission synchronously calls every lambda, and fences/semaphores are host condition variables (`include/digitor/commands.hpp:8-25`; `src/gpu/commands.cpp:4-16`). |
| Vulkan native resources/device | Implemented but unverified | Conditional backend creates an instance/device and images, buffers, memory, views, and samplers (`src/gpu/vulkan_backend.cpp:5-10`). No queue family is retained, no queue/command pool exists, and the backend interface has no submission API. |
| Vulkan commands/shaders/pipelines/bindings/dispatch/draw/transitions/readback | Not implemented | The Vulkan translation unit only implements resource methods and device wait-idle (`src/gpu/vulkan_backend.cpp:6-12`); native execution is absent from `IRenderBackend` (`src/gpu/gpu_backend.hpp:12-27`). |
| D3D12 native resources/device | Implemented but unverified | Device and committed texture/buffer resources are created (`src/gpu/d3d12_backend.cpp:14-28`). The “sampler” is only a heap-allocated copy of `DigitorSamplerDesc`, not a descriptor heap entry (`src/gpu/d3d12_backend.cpp:19`). |
| D3D12 commands/shaders/PSOs/root signatures/descriptors/dispatch/draw/transitions/readback | Not implemented | No command queue/list/allocator, PSO, root signature, descriptor heap, resource barrier, execute, fence, or copy/readback operation exists in the backend (`src/gpu/d3d12_backend.cpp:14-28`). |
| Metal native resources/device | Implemented but unverified | Metal creates textures, buffers, and sampler states (`src/gpu/metal_backend.mm:5-13`). |
| Metal command queues/buffers/libraries/pipelines/bindings/dispatch/draw/synchronization/transitions/readback | Not implemented | There is no `MTLCommandQueue`, command buffer/encoder, shader library/function, pipeline state, presentation, or readback in the complete backend (`src/gpu/metal_backend.mm:5-13`). |
| GLES native resources/context probe | Implemented but unverified | GLES checks an externally current context and creates textures/buffers/samplers (`src/gpu/gles_backend.cpp:5-10`). It does not create or own EGL context/surface state. |
| GLES shader programs/bindings/dispatch/draw/synchronization/transitions/readback | Not implemented | The GLES backend contains allocation/map/delete calls only; capability reporting does not report compute (`src/gpu/gles_backend.cpp:5-10`). |
| Shader compilation/reflection | Placeholder/stub | Text languages are accepted when text contains `main`; bindings/workgroup size are regex-parsed. SPIR-V only gets byte packing and magic validation (`src/gpu/shader.cpp:4-7`). No GLSL/HLSL/MSL compiler is invoked. |
| Pipeline creation/cache | Placeholder/stub | A pipeline “handle” is only a hash of shader hashes (`src/gpu/shader.cpp:7`); no native pipeline is created. |
| GPU resource readback/presentation | Not implemented | Public/internal resource APIs expose create/destroy and host-visible buffer map only (`include/digitor/digitor.h:118-169`; `src/gpu/gpu_backend.hpp:19-26`). There is no texture upload/readback or surface API. |

Native allocation itself may cause driver work; this audit does **not** classify it as rendering.
The fallback `DeviceBackend` can also report a discovered GPU while supporting no resources at
all (`src/gpu/gpu_backend.cpp:49-57`, `202-207`).

## 2. Preview and export

| Claim/capability | Status | Evidence and finding |
|---|---|---|
| Common orchestration class | Implemented and verified | Both wrappers call the same `SharedRenderer::render`; host tests verify preview caching, not export equality (`src/render/renderer.cpp:8-11`; `tests/test_editor.cpp:5`). |
| Same decoded source frame | Not implemented | `RenderRequest` has no decoder/source input and `SharedRenderer` constructs a fresh empty output (`include/digitor/renderer.hpp:7-10`; `src/render/renderer.cpp:9`). Timeline and decoders are not connected. |
| Same native shader graph | Not implemented | `SharedRenderer` executes the CPU callback `RenderGraph`; there is no native shader graph (`src/render/renderer.cpp:9`; `src/gpu/render_graph.cpp:12`). |
| Pixel-identical preview/export | Implemented but unverified | Shared orchestration makes equality possible for identical requests, but preview applies its stored transform while export uses a default transform; there is no numerical preview-versus-export pixel test (`src/render/renderer.cpp:10-11`). Therefore the public identity claim is unproven and not generally guaranteed. |
| Real preview surface | Not implemented | Preview returns a cached in-memory `shared_ptr<VideoFrame>`; no window, swapchain, layer, surface, or host texture bridge exists (`include/digitor/renderer.hpp:11-12`; `src/render/renderer.cpp:10`). |
| Processed GPU frame to real encoder | Not implemented | No GPU frames exist in this path and there is no encoder API/integration (`include/digitor/renderer.hpp:13-15`; `src/render/renderer.cpp:11`). |
| MP4/MOV/MKV export | Placeholder/stub | Files begin with `DIGITOR` and text metadata regardless of selected container (`src/render/renderer.cpp:11`); they are not standards-compliant media. |
| Image sequence export | Placeholder/stub | Output names end in `.rgba` and contain host-native raw `Color` structs, with no image header, metadata, conversion, or codec (`src/render/renderer.cpp:11`). |

## 3. Media decoding and encoding

| Capability | Status | Evidence and finding |
|---|---|---|
| FFmpeg package detection/linking | Implemented but unverified | CMake detects avcodec/avformat/avutil/swscale/swresample and defines `DIGITOR_HAS_FFMPEG` (`CMakeLists.txt:35-44`). The audited environment did not expose those packages. |
| Demux, packet reading, video/audio decode | Placeholder/stub | Decoder constructors accept any nonempty path without opening it. `decode(n)` returns number/PTS only; width, height, pixels, and audio samples remain empty (`src/media/media.cpp:23-28`). |
| Timestamp/time-base handling | Placeholder/stub | PTS is assigned directly from requested frame number with no stream time base (`src/media/media.cpp:25-26`). |
| Pixel/sample conversion | Not implemented | No libswscale/libswresample calls occur anywhere; linking the libraries does not use them (`src/media/media.cpp:1-35`). |
| Hardware decode | Placeholder/stub | Platform macros label the decoder DXVA/VideoToolbox/MediaCodec and set `hardware_accelerated=true`; no hardware context or decoder is created (`src/media/media.cpp:5-21`). |
| Software fallback | Placeholder/stub | Selection changes metadata to “FFmpeg software,” but uses the same empty-frame class (`src/media/media.cpp:16-28`). |
| Encoding and muxing | Not implemented | No encoder/muxer implementation or FFmpeg encoding calls exist; export writes private/raw output (`src/render/renderer.cpp:11`). |

## 4. Color accuracy

| Capability | Status | Evidence and finding |
|---|---|---|
| sRGB transfer helpers | Implemented but unverified | CPU piecewise transfer functions exist, but lack conformance vectors and handling policy for negative/out-of-range values (`src/gpu/color.cpp:4-5`). |
| Exposure, contrast, gamma, lift/gain/offset | CPU simulation/reference only | Scalar CPU math exists (`src/gpu/color.cpp:6-7`); parameters are combined in a single non-documented order and “GPU” dispatch calls that CPU function. Lift/gamma/gain are scalar, not per-channel controls (`include/digitor/color.hpp:5-7`). |
| Temperature/tint | CPU simulation/reference only | Implemented as simple channel offsets rather than a specified chromatic adaptation/color-temperature model (`src/gpu/color.cpp:6`). |
| Saturation/vibrance/hue | CPU simulation/reference only | CPU formulas exist without color-space/accuracy specification (`src/gpu/color.cpp:6`). |
| Curves | Not implemented | No curves field/API or implementation exists in `ColorGrade` (`include/digitor/color.hpp:5-7`). |
| HSL qualifier | Not implemented | No qualifier type/API/implementation exists in public color interfaces (`include/digitor/color.hpp:1-8`). |
| 1D/3D `.cube` LUT | Implemented and verified | CPU parser and nearest/linear/tetrahedral sampling exist; unit tests cover small identity examples (`src/gpu/lut.cpp:14-22`; `tests/test_v2.cpp:9-10`). Broader format/conformance coverage remains absent. |
| Linear-light pipeline | Placeholder/stub | Transfer helpers exist, but `grade_image_cpu` does not call them and no renderer establishes a working color space (`src/gpu/color.cpp:5-7`). |
| Native GPU/CPU parity | Not implemented | `grade_image_gpu` and `apply_lut_gpu` enqueue the exact CPU routines (`src/gpu/color.cpp:7`; `src/gpu/lut.cpp:21-22`). Tests compare a CPU call with that same CPU code through a callback (`tests/test_engine.cpp:60`; `tests/test_v2.cpp:9`). |

## 5. Effects and node graph

| Capability | Status | Evidence and finding |
|---|---|---|
| Blur/sharpen/glow/distortion/noise/grain/aberration/vignette/motion blur | CPU simulation/reference only | Effects are nested CPU loops over a copied vector; `apply_effect_gpu` records a lambda calling them (`src/gpu/effects.cpp:6-12`). |
| Native shader execution | Not implemented | Node `shader` is serialized metadata only; evaluation exclusively invokes `NodeProcessor` callbacks (`src/gpu/node_graph.cpp:14-16`). |
| Serial dependency ordering | Implemented and verified | DFS topological evaluation rejects cycles and resolves inputs before consumers; tests cover a two-node chain (`src/gpu/node_graph.cpp:10-14`; `tests/test_v2.cpp:8`). |
| Parallel node execution | Not implemented | Evaluation is a single sequential `for` loop (`src/gpu/node_graph.cpp:14`). |
| Frame cache correctness | Placeholder/stub | Cache key includes node/frame/width/height only and omits `NodeContext::values`, processor state, input content, shader identity, and external media revisions (`include/digitor/node_graph.hpp:14-27`; `src/gpu/node_graph.cpp:14`). Graph edits clear all cache, but context changes can return stale pixels. |
| Deterministic serialization | Implemented and verified | Nodes are sorted by numeric ID and quoted fields are serialized; a round-trip shape/evaluation test exists (`src/gpu/node_graph.cpp:15-16`; `tests/test_v2.cpp:8`). Processor code/state and context are intentionally not serialized, so resolver behavior is external. |
| Actual frame processing | CPU simulation/reference only | Arbitrary callbacks return `vector<Color>`; no validation enforces frame dimensions and no native resources are involved (`include/digitor/node_graph.hpp:10-15`; `src/gpu/node_graph.cpp:14`). |

## 6. Timeline

| Capability | Status | Evidence and finding |
|---|---|---|
| Track/clip edit model and integral frame positions | Implemented and verified | Add/move/overwrite/insert, ripple/roll/slip/slide, keyframes, undo/redo are CPU data operations with basic tests (`src/editor/timeline.cpp:6-20`; `tests/test_editor.cpp:5`). |
| Real decoded media integration | Not implemented | Clips store only a source string; Timeline owns no decoder and renderer accepts no timeline (`include/digitor/timeline.hpp:8-28`; `include/digitor/renderer.hpp:7-15`). |
| Frame-accurate scheduling | Placeholder/stub | Integral `FrameNumber` storage exists, but rational rate is not used to map media PTS/time bases or select decoder frames (`include/digitor/timeline.hpp:13-28`; `src/editor/timeline.cpp:6-20`). |
| Clip compositing and transitions | Not implemented | There is no transition type, compositing method, opacity/blend policy, or timeline render method (`include/digitor/timeline.hpp:8-28`). |
| Audio synchronization/mixing | Not implemented | Timeline has no audio model or audio render path (`include/digitor/timeline.hpp:8-28`). |
| Cache invalidation | Not implemented | Timeline has no rendering cache/dependency invalidation; its undo snapshots only tracks (`include/digitor/timeline.hpp:25-28`). Preview cache invalidates only on transform, while graph/source edits have no revision key (`include/digitor/renderer.hpp:11-12`). |

## 7. Cross-platform support

See `docs/platform_support.md`. Source conditionals and CMake branches count only as configured;
they do not establish that a platform compiles, runs, decodes, encodes, previews, or performs GPU
work (`CMakeLists.txt:46-73`). No platform/device CI configuration exists in this repository.

## 8. Public API and ABI

| Capability | Status | Evidence and finding |
|---|---|---|
| C-compatible declarations/opaque handles | Implemented and verified | `extern "C"`, fixed-width fields, opaque resource types, and static/shared export macros exist; a C compile/link test covers types/symbol references (`include/digitor/digitor.h:6-169`; `tests/test_c_header.c:1-49`). |
| Exception safety across C ABI | Placeholder/stub | Allocation wrappers catch only `std::bad_alloc`; backend/resource calls and handle-set insertion can throw other exceptions. Lifecycle functions call C++ directly with no catch-all (`src/ffi/digitor_c_api.cpp:28-36`, `95-165`). Any escaping exception across C is invalid API behavior. |
| Handle ownership/lifetime | Implemented but unverified | Context resource counts and registries reject basic double destroy/live-context destroy (`src/core/render_context.hpp:18-34`; `src/ffi/digitor_c_api.cpp:18-21`, `148-165`). Registries are global, and handles carry no generation/type magic. |
| Thread safety | Placeholder/stub | Registry membership checks release the mutex before dereferencing handles, so concurrent destroy/use can cause use-after-free (`src/ffi/digitor_c_api.cpp:66-76`). Engine calls are serialized, and buffer mapping has a mutex, but there is no end-to-end handle concurrency contract (`src/core/engine.cpp:11-77`; `src/core/resources.cpp:30-56`). |
| Static/shared behavior | Implemented but unverified | `add_library` follows `BUILD_SHARED_LIBS`, static consumers receive `DIGITOR_ENGINE_STATIC`, and install exports exist (`CMakeLists.txt:20-34`, `75-124`). Audited tests exercised only the default static host build; symbol visibility/package consumption are untested. |
| ABI stability | Not implemented | There is no ABI policy, struct size/version fields, symbol-versioning, compatibility suite, or baseline artifact. The test asserts minimum/current layouts rather than compatibility (`tests/test_c_header.c:7-22`). |
| Version accuracy | Placeholder/stub | CMake and API consistently report `2.0.0` (`CMakeLists.txt:3-7`; `src/ffi/digitor_c_api.cpp:24-26`), but no documented semantic/ABI meaning or production qualification supports the number. Per instruction, this audit does not change it. |

## 9. Test classification

| Test group | Status | What it actually validates |
|---|---|---|
| Native GPU work | Not implemented | No test creates a real backend and submits native work or validates GPU pixels. Backend policy uses `FakeBackend` factories (`tests/test_engine.cpp:14-49`). |
| Commands/render graph/color | CPU simulation/reference only | Tests execute lambdas, check scheduling counters, and compare CPU math to the same CPU math behind `*_gpu` (`tests/test_engine.cpp:57-60`). |
| LUT/effects/node graph | CPU simulation/reference only | Tests validate selected scalar pixels produced by CPU callbacks; no native backend is involved (`tests/test_v2.cpp:8-11`). |
| Timeline/preview | Implemented and verified | Tests cover basic edit operations and in-memory preview cache size/build count, but not decoded pixels, export equality, surfaces, compositing, transitions, or audio (`tests/test_editor.cpp:5`). |
| C API/resources | Implemented and verified | Tests cover CPU lifecycle, validation, mapping, double destroy, and header compilation (`tests/test_engine.cpp:64-122`; `tests/test_c_header.c:1-49`). They do not test concurrent handles, exception containment, shared-library ABI, or native resources. |
| Real media | Not implemented | No fixture is demuxed/decoded/probed and no encoded output is played or independently parsed. |

## Audit limits

- “Implemented and verified” means verified by source-aligned automated tests executed on the
  Linux CPU host, not independently certified or production-ready.
- Native resource code is “Implemented but unverified” because it was neither cross-compiled nor
  run on target devices during this audit.
- Findings are scoped to the supplied commit. Generated/vendor code and external downstream
  integrations were not present and therefore cannot substantiate repository claims.
