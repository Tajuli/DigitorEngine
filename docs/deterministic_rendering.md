# Deterministic rendering validation policy

This policy defines test acceptance; it is not a claim that all backends currently satisfy it.
Every result records engine revision, backend/device/driver, compiler, build options, color space,
pixel format, dimensions, and fixture identity. Comparisons use a separately maintained CPU
reference evaluated in IEEE-754 binary64, converting to the destination format only at readback.

## A. Exact operations

Clear, copy, integer conversion, and tightly packed RGBA8 readback require byte-for-byte equality,
including row order and alpha. Dimensions and `width * height * bytes_per_pixel` are checked for
overflow before allocation. Padding is excluded from tightly packed comparisons and must never be
serialized. Stable serialized/cache identities use SHA-256 over a version tag, explicit little-endian
field encodings, UTF-8 byte strings prefixed by a 64-bit length, and canonical payload bytes. Language
runtime hashes (`std::hash`) must not be persisted or used between processes.

## B. Floating-point operations

Color transforms, curves, LUTs, and effects are compared against the binary64 CPU reference after
both results are converted to linear RGBA32F. Unless a more restrictive operation profile exists:

* finite values pass when absolute error is at most `2e-5`, or relative error is at most `2e-4`
  using `abs(a-b)/max(abs(reference), 1e-6)`; diagnostics also report float32 ULP distance;
* an 8-ULP ceiling applies where the reference operation maps directly to float32 arithmetic;
* whole-image acceptance additionally requires PSNR >= 80 dB and SSIM >= 0.9999 per RGB channel;
* NaN is always a failure. Infinity is accepted only when the specification explicitly produces an
  infinity of the same sign; normal rendering must clamp it before storage;
* subnormal inputs/results may be flushed to signed zero. Comparisons treat values below the minimum
  normal float32 magnitude as zero;
* contraction/FMA and reassociation are disabled in the exact reference. Production shaders may use
  FMA but must meet the tolerances. Compiler fast-math is forbidden in validation builds;
* conversions round to nearest, ties to even. UNORM clamps to `[0,1]` before rounding; float paths do
  not clamp unless the operation specifies it. Alpha follows the operation's stated premultiplication.

Golden failures retain numerical metrics and a difference image. Threshold changes require reviewed
reference evidence, never merely a new platform result. Cross-platform byte identity is **not claimed**
for floating-point GPU rendering; it may be claimed for a named operation/backend only after independent
multi-platform qualification.

## Shader determinism
Shader cache identities include canonical HLSL, every include content identity, ordered compile controls, exact compiler version, target, specialization values, and shader ABI. Native pipelines additionally include all attachment/fixed-function and device compatibility state.

## v4.5 color numerical policy

Metadata resolution, graph ordering/identity on the same ABI, specified integer
range normalization, and RGBA8 clear/copy require byte equality. Floating color
is compared, not called byte-identical. FP32 qualification limits per operation
are: transfer and YUV max absolute `2e-6`, max relative `2e-5`, RMS `5e-7`;
matrix/adaptation max absolute `2e-6`, relative `2e-5`, RMS `5e-7`; PQ/HLG
round-trip max absolute `8e-6`, relative `5e-5`, RMS `2e-6`. Image qualification
additionally requires PSNR >= 100 dB and SSIM >= 0.99999. ULP is diagnostic
because transcendental libraries differ. FP16 limits are not yet qualified.

Fast-math is disabled. Reference matrix terms are evaluated row-major in written
multiply/add order; shader contraction/FMA must be disabled for reference runs
and separately reported otherwise. Round-to-nearest-even is assumed. Denormals
are preserved by the CPU reference; a flushing GPU must be reported. No implicit
clamp occurs. Non-finite values are rejected, PQ/HLG reject negatives, SDR
functions use their documented signed extension. GLES shaders must use `highp`;
FP16 and FP32 results are reported separately. Cross-GPU bit identity is not
claimed. A report must include maximum absolute/relative/RMS errors, PSNR, SSIM,
first failure, backend, operation and spaces.

## v4.6.1 native grade qualification

The source-level audit and exact qualification truth table are maintained in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md). This host audit did
not execute qualifying Vulkan, D3D12, Metal, GLES, Android, or iOS hardware.
Native compilation is not reported as pixel validation; FP16 is unsupported.
The internal provenance/failure seams prove that failures do not silently run
the CPU reference in non-hardware tests.

## RGB Curves

Points retain caller order and must already have strictly increasing x. Coefficients and LUT entries are evaluated in documented source order at endpoint-inclusive sample locations. The C++ build does not enable fast-math; source expressions do not request FMA contraction and do not flush denormals explicitly. Stable identity uses hexadecimal binary32 bits, not locale formatting or `std::hash`. NaN/infinity inputs propagate, finite overflow in LUT compilation fails, and extrapolation is explicit. Cross-platform byte identity is not claimed; future native FP32 qualification uses numerical tolerances.
