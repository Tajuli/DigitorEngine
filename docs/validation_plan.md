# Production validation plan

Production claims are blocked until the following reproducible gates pass. Test artifacts must
record commit, toolchain, dependency versions, OS/device/GPU/driver, command line, logs, and
checksums. A build or device probe alone is not GPU-rendering validation.

## 1. Build and packaging gates

1. Build Debug/Release and static/shared variants with warnings-as-errors and sanitizers where
   supported: MSVC x64 Windows; Android NDK arm64-v8a/x86_64; Xcode macOS arm64/x86_64; iOS
   device arm64 and simulator. Consume each installed CMake package from a separate C and C++
   project and inspect exported symbols.
2. Build every intended backend explicitly, both with and without FFmpeg. Fail configuration
   when a requested backend/codec dependency is unavailable rather than silently changing claims.
3. Run ASan/UBSan/TSan on portable paths, Windows Application Verifier as applicable, Android
   HWASan, and Apple sanitizers. Exercise repeated initialization, failure injection, device loss,
   memory pressure, and teardown with live/in-flight resources.

## 2. Native GPU execution gates

For **each** Vulkan, D3D12, Metal, and GLES backend:

1. Capture a minimal upload → compute effect → copy/readback submission with RenderDoc, PIX,
   Xcode GPU Capture, or AGI. The capture must show native queues, command buffers/encoders,
   compiled pipelines, bindings, dispatch/draw, barriers/transitions, and synchronization.
2. Run Vulkan validation layers, D3D12 debug layer + GPU-based validation, Metal API validation,
   and GLES debug/error validation with zero errors. Test timeout/device-loss recovery.
3. Exercise every texture format/usage, upload/download alignment, aliasing, read/write hazards,
   multiple frames in flight, fence/semaphore ordering, resizing, and destruction after completion.
4. Verify shader compilation failures and reflection/layout agreement using real toolchains; cache
   keys must include backend, compiler/version/options, entry point, specialization, and layout.
5. Run on representative hardware: Windows NVIDIA/AMD/Intel; Android Vulkan and GLES devices
   from at least two GPU vendors/API levels; Apple Silicon macOS; supported iPhone and iPad.

## 3. Pixel and color gates

1. Create an independent double-precision oracle and published golden vectors for transfer
   functions, exposure, contrast, per-channel lift/gamma/gain/offset, temperature/tint, curves,
   qualifier, LUT modes, alpha, range, NaN/Inf, and out-of-gamut behavior.
2. Feed deterministic ramps, color charts, impulses, edges, noise seeds, and real decoded frames
   through CPU and each native backend. Read back actual GPU textures and compare max/mean error,
   PSNR/SSIM, and exact hashes where integer-exact behavior is promised. Define tolerances before
   testing and never compare a CPU wrapper to the same CPU routine as “GPU parity.”
3. Validate the declared color space end-to-end using tagged SDR/linear/HDR fixtures, range and
   chroma siting, ICC/NCLC metadata where applicable, and independent tools.
4. Render identical preview/export requests from the same immutable decoded-frame ID and graph
   revision. Numerically compare every pixel and metadata; separately test transforms if preview
   display transforms are intentionally excluded from export.

## 4. Media gates

1. Check in or reproducibly generate small licensed fixtures spanning H.264/H.265/AV1 as
   supported, CFR/VFR, B-frames, 8/10-bit, 4:2:0/4:2:2/4:4:4, rotation/SAR, MP4/MOV/MKV, and
   AAC/PCM/Opus with known packet/frame PTS and hashes.
2. Test open/probe, stream selection, packet iteration, decode, seek/flush, EOF, corrupt/truncated
   input, timestamp/time-base conversion, frame reordering, pixel conversion, audio resampling,
   channel layout, and A/V drift over long synthetic media.
3. On each target, prove hardware decode with API traces/frame-memory type, compare its decoded
   pixels/timestamps to software decode within declared tolerances, and force/test fallback.
4. Encode MP4, MOV, MKV, PNG/JPEG/TIFF (only formats actually promised), then validate with an
   independent `ffprobe`, decode-to-hash, standards-aware players, duration/frame count/PTS,
   codec/container tags, color/audio metadata, and A/V synchronization. Reject private/raw output
   when a standard format was requested.
5. Test GPU-frame encoder interop and explicit readback paths for lifetime, synchronization,
   format conversion, backpressure, cancellation, and deterministic flushing/finalization.

## 5. Graph, timeline, preview, and cache gates

1. Graph tests: serial/branched/parallel DAGs, cycle rejection, deterministic serialization across
   processes/platforms, processor/shader state restoration, native shader execution, invalid
   dimensions, concurrent evaluation, and cancellation.
2. Cache keys/invalidation must cover all inputs: source revision/frame/PTS, graph topology and
   parameters, node context values, dimensions/format/color metadata, timeline edit generation,
   decode mode, and effect seed. Use mutation tests proving no stale output survives each change.
3. Timeline tests: rate/time-base conversion, VFR sources, boundaries, overlapping clips, blend
   modes, transitions, nested effects, reverse/rate changes if promised, audio mixing/resampling,
   and hours-long A/V drift. Compare scheduled source PTS to independent expected tables.
4. Present real frames through Win32, Android, macOS, and iOS surfaces. Test resize/orientation,
   background/foreground, device loss, color management, pacing, scrubbing, and dropped frames.

## 6. C API/ABI gates

1. Wrap every exported entry point in a no-throw boundary mapping all failures to documented
   results; add injected exceptions for allocation, registries, backend, and destructors.
2. Specify ownership, parent-child lifetime, nullability, callback rules, and thread/concurrent
   destroy behavior. Stress with simultaneous create/map/unmap/destroy/shutdown under TSan.
3. Establish ABI baselines per target: exported symbol list, calling convention, enum values,
   struct size/alignment/offset snapshots, and old-client/new-library compatibility tests.
4. Define semantic versioning and feature/capability negotiation. Only then describe the API as
   stable or interpret `2.0.0` as a production compatibility promise.

## Release evidence checklist

A production claim requires links to all platform build artifacts, device logs, native GPU
captures, validation-layer logs, pixel reports/goldens, independent media probe/playback results,
surface recordings/screenshots, sanitizer results, ABI reports, and known limitations. Missing
evidence must be reported as “unverified,” never inferred from conditional source.
