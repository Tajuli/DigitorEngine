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
