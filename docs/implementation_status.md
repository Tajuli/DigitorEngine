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
