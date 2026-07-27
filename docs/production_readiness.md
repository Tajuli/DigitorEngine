# Production stabilization qualification

Status vocabulary is strictly: **Implemented and verified**, **Implemented but unverified**, **CPU
reference only**, **Placeholder/stub**, or **Not implemented**. Verification is limited to named CI/jobs;
version numbers are not readiness evidence.

| Area | Status | Qualification limit |
|---|---|---|
| Linux CPU builds and unit tests | Implemented and verified | GCC/Clang CI matrix; hardware GPU excluded |
| Windows/macOS desktop builds | Implemented but unverified | CI configured; no local evidence in this document |
| Install/exported CMake package | Implemented and verified | Dedicated installed C and C++ consumers |
| FFmpeg software decode | Implemented and verified | Generated MP4/MOV/MKV/WAV plus malformed data in FFmpeg jobs |
| Native GPU allocation | Implemented but unverified | Hardware/driver qualification remains separate |
| Color/effects/LUT execution | CPU reference only | GPU floating-point identity is not claimed |
| Native rendering pipelines | Placeholder/stub | Backend-dependent partial paths only |
| Standard media export | Implemented but unverified | Requires independent interoperability coverage |
| Stable public ABI | Not implemented | No compatibility baseline or symbol-version policy |
| Plugin loading/SDK | Not implemented | Design is documented only |

The public structs currently have no `size`/ABI-version fields. Callers must compile against the matching
header/library; additions cannot yet be negotiated. C handles enforce basic registry/type membership but
concurrent destroy/use is not guaranteed safe. Long-term ABI stability is explicitly not claimed.

## Color science v4.5

The CPU mathematical foundation is implemented and locally tested. Production
GPU readiness is blocked on native graph compilers, backend fixtures, FP16/FP32
qualification and hardware CI. PQ/HLG functions do not constitute an HDR display
pipeline. Performance targets and native timings are not available; no real-time
claim is made.

## v4.6.1 native grade qualification

The source-level audit and exact qualification truth table are maintained in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md). This host audit did
not execute qualifying Vulkan, D3D12, Metal, GLES, Android, or iOS hardware.
Native compilation is not reported as pixel validation; FP16 is unsupported.
The internal provenance/failure seams prove that failures do not silently run
the CPU reference in non-hardware tests.

## RGB Curves readiness

The immutable specification, CPU reference, deterministic LUT/cache, tests, and explicit CPU Render Graph pass are ready. Native backends, preview use, provenance, numerical GPU comparison, and performance qualification remain release blockers. Consequently the project version remains 4.6.1 and this work must not be described as a complete production v4.7.0 release.

The canonical curve shader, native cache key/ownership model, expanded provenance schema and backend-neutral graph pass now exist. Native backend artifact creation and hardware pixel qualification remain blockers; therefore 4.7.0 is not declared by this revision and Preview = Export remains unclaimed.
