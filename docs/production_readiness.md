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
