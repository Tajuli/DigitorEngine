# Implementation status — v4.6.1

The CPU equations in `src/gpu/color.cpp` remain authoritative. The canonical
compute source is `src/gpu/shaders/color_pipeline.hlsl`; native backend adapters
preserve its operation order. “Implemented, hardware-unverified” means real
native commands exist but this Linux build supplied no qualifying hardware run.
It must never be interpreted as “Passed”.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Evidence | Status |
|---|---|---|---|---|---|---|---|
| Exposure | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Contrast | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Saturation | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Temperature | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Tint | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Lift | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Gamma | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Gain | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Offset | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| HSV (hue rotation) | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Matrix (hue rotation) | yes | implemented | implemented | implemented | implemented | canonical shader; CPU comparison | Implemented, hardware-unverified |
| ToneMap | yes (passthrough) | no stage | no stage | no stage | no stage | color-science passthrough test | CPU reference only |
| Vibrance | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |

RGB curves, primary/log wheels, HSL qualifier, and LUT execution are **not
implemented** by this milestone. The graph rejects non-identity future-stage
parameters rather than skipping them or falling back to CPU. Preview/export
equivalence is not claimed. The validation report contains maximum absolute and
relative error, RMS, PSNR, SSIM, first failing pixel, worst pixel, backend, and
precision. Native hardware remains unverified on all four backends for this
commit.

## v4.6.1 native grade qualification

The source-level audit and exact qualification truth table are maintained in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md). This host audit did
not execute qualifying Vulkan, D3D12, Metal, GLES, Android, or iOS hardware.
Native compilation is not reported as pixel validation; FP16 is unsupported.
The internal provenance/failure seams prove that failures do not silently run
the CPU reference in non-hardware tests.

## RGB Curves implementation truth table

CI/hardware environment for implemented rows: local Ubuntu GCC 13 CPU-only; no GPU device or driver was available. `test_rgb_curves` is deterministic non-hardware evidence; exact identity and alpha checks measured zero error. “Unsupported” backend cells have no numerical result and are not verified claims.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Test Evidence | Numerical Result | Status |
|---|---|---|---|---|---|---|---|---|
| Curve descriptor validation | `compile_one` | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves` malformed cases, Ubuntu GCC 13 CPU | accepted cases exact; invalid rejected | Production implementation, software-adapter verified |
| Control-point canonicalization | strict caller order | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | deterministic | Production implementation, software-adapter verified |
| Monotone cubic coefficients | `compile_one` PCHIP | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves` flat/steep sweep, CPU | monotonic within 1e-6 | Production implementation, software-adapter verified |
| 256-sample FP32 LUT | `CompiledRgbCurves::compile` | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | allocation/identity pass | Production implementation, software-adapter verified |
| 1024-sample FP32 LUT | default | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | exact identity | Production implementation, software-adapter verified |
| 4096-sample FP32 LUT | supported | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | size/content pass | Production implementation, software-adapter verified |
| Master curve | channel-wise | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | alpha exact | CPU reference only |
| Red curve | independent | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | G/B isolation exact | CPU reference only |
| Green curve | independent | Unsupported | Unsupported | Unsupported | Unsupported | identity fixture, CPU | exact identity | CPU reference only |
| Blue curve | independent | Unsupported | Unsupported | Unsupported | Unsupported | identity fixture, CPU | exact identity | CPU reference only |
| Master + RGB combined | fixed order | Unsupported | Unsupported | Unsupported | Unsupported | source/order review | not GPU measured | CPU reference only |
| Negative-value behavior | explicit extrapolation | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | identity exact | CPU reference only |
| Over-range behavior | explicit extrapolation | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | identity exact | CPU reference only |
| Alpha preservation | bit-preserved | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | zero error | Production implementation, software-adapter verified |
| Render Graph integration | explicit CPU pass | Unsupported | Unsupported | Unsupported | Unsupported | `add_rgb_curves_cpu_pass` source review | not numerical | CPU reference only |
| Node-graph contract | immutable schema v1 | Unsupported | Unsupported | Unsupported | Unsupported | serialization/cache test | stable identity | Interface/design only |
| Pipeline cache | not needed | Unsupported | Unsupported | Unsupported | Unsupported | none | none | Not implemented |
| LUT-resource cache | CPU weak cache | Unsupported | Unsupported | Unsupported | Unsupported | cold/change/warm `test_rgb_curves` | pointer-equal warm hit | CPU reference only |
| Preview consumption | Unsupported | Unsupported | Unsupported | Unsupported | Unsupported | none | none | Not implemented |
| FP32 | implemented | Unsupported | Unsupported | Unsupported | Unsupported | `test_rgb_curves`, CPU | identity/alpha exact | CPU reference only |
| FP16, if implemented | Unsupported | Unsupported | Unsupported | Unsupported | Unsupported | none | none | Unsupported |

### v4.7 native execution development truth table

Environment: Ubuntu/GCC 13 CPU-only container; no GPU/device/driver execution. The canonical shader source is not hardware evidence. Numerical GPU results are therefore `not measured`, honestly, and version 4.7.0 remains gated.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Evidence | Numerical Result | Status |
|---|---|---|---|---|---|---|---|---|
| Native FP32 LUT resource | separate CPU LUT | Not implemented | Not implemented | Not implemented | Not implemented | native owner/cache source + CPU test | not measured | Not implemented |
| 256-sample LUT execution | yes | Not implemented | Not implemented | Not implemented | Not implemented | CPU deterministic test | CPU exact identity | CPU reference only |
| 1024-sample LUT execution | yes | Not implemented | Not implemented | Not implemented | Not implemented | CPU deterministic test | CPU exact identity | CPU reference only |
| 4096-sample LUT execution | yes | Not implemented | Not implemented | Not implemented | Not implemented | CPU deterministic test | CPU allocation passes | CPU reference only |
| Master curve | yes | Not implemented | Not implemented | Not implemented | Not implemented | canonical shader source, not executed | not measured | CPU reference only |
| Red curve | yes | Not implemented | Not implemented | Not implemented | Not implemented | canonical shader source, not executed | not measured | CPU reference only |
| Green curve | yes | Not implemented | Not implemented | Not implemented | Not implemented | canonical shader source, not executed | not measured | CPU reference only |
| Blue curve | yes | Not implemented | Not implemented | Not implemented | Not implemented | canonical shader source, not executed | not measured | CPU reference only |
| Master + RGB | yes | Not implemented | Not implemented | Not implemented | Not implemented | canonical source order | not measured | CPU reference only |
| Identity bypass | exact | Not implemented | Not implemented | Not implemented | Not implemented | CPU identity test | zero error CPU | CPU reference only |
| Negative values | yes | Not implemented | Not implemented | Not implemented | Not implemented | CPU extrapolation test | passes CPU | CPU reference only |
| Over-range values | yes | Not implemented | Not implemented | Not implemented | Not implemented | CPU extrapolation test | passes CPU | CPU reference only |
| Alpha preservation | exact | Not implemented | Not implemented | Not implemented | Not implemented | CPU test/canonical shader | zero error CPU | CPU reference only |
| Render Graph integration | CPU pass | backend-neutral node | backend-neutral node | backend-neutral node | backend-neutral node | graph pass source | non-numerical | Implemented, hardware-unverified |
| Native LUT cache | n/a | device-key model | device-key model | device-key model | device-key model | cold/warm/device-key unit test | non-numerical | Implemented, hardware-unverified |
| Preview consumption | CPU only | Not implemented | Not implemented | Not implemented | Not implemented | none | not measured | Not implemented |
| Provenance | CPU fields | schema only | schema only | schema only | schema only | provenance source | not measured | Placeholder/stub |
| Failure injection | CPU validation | Not implemented | Not implemented | Not implemented | Not implemented | enum coverage only | not measured | Not implemented |
| FP32 | yes | shader contract only | shader contract only | shader contract only | shader contract only | canonical HLSL | not measured | Placeholder/stub |
| FP16 | Unsupported | Unsupported | Unsupported | Unsupported | Unsupported | no implementation | none | Unsupported |
