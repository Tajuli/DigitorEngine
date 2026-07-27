# RGB Curves specification (parameter version 1)

## Canonical mathematics

RGB Curves consumes and produces unbounded IEEE-754 binary32 RGB in the engine's scene-linear working space. Alpha is copied bit-for-bit. Four scalar functions are applied in the fixed order **Master to each of R, G, B, then Red, Green, Blue independently**. This is a channel-wise master, not a luminance-preserving adjustment.

A curve has 2–64 strictly increasing finite `(x,y)` points, an explicit finite domain with `minimum < maximum`, enabled state, monotone-cubic interpolation, and constant or linear extrapolation. Points must lie in the domain; inputs are never implicitly clamped to `[0,1]`. Constant extrapolation holds the domain endpoint value. Linear extrapolation continues the deterministic endpoint derivative. Thus negative and over-range scene-linear values survive an identity curve and otherwise follow the selected policy. Finite output overflow is rejected during compilation. NaN and infinity inputs propagate unchanged through that scalar stage. Disabled curves are identity.

Duplicate x values, descending x, non-finite data, invalid domains/enums, unsupported LUT sizes, and point counts outside 2–64 are errors. Identity means every point has `x == y`; it is evaluated exactly as `f(x)=x`, including outside the domain.

## Interpolation

The implementation uses shape-preserving piecewise cubic Hermite interpolation (PCHIP/Fritsch–Carlson family). For `h_i=x_(i+1)-x_i` and `d_i=(y_(i+1)-y_i)/h_i`, an interior derivative is zero when adjacent secants are zero or have unlike signs. Otherwise

`m_i=(w1+w2)/(w1/d_(i-1)+w2/d_i)`, where `w1=2h_i+h_(i-1)` and `w2=h_i+2h_(i-1)`.

One-sided endpoint derivatives use `( (2h0+h1)d0-h0d1 )/(h0+h1)`, clipped to zero on a sign reversal and to `3d0` when the adjacent secants change sign. Two-point curves use their secant. A segment is evaluated, in source order, as

`(2t^3-3t^2+1)y_i + (t^3-2t^2+t)h_i m_i + (-2t^3+3t^2)y_(i+1) + (t^3-t^2)h_i m_(i+1)`.

This handles flat, steep, increasing, and decreasing portions without monotonic-segment overshoot or division by zero.

## FP32 LUT and sampling

Supported sizes are exactly 256, 1024 (preview default), and 4096. Each curve is a separate immutable FP32 array; no RGBA8 packing or FP16 mode exists. Sample `i` represents `domain_min + (domain_max-domain_min)*i/(N-1)`, including both endpoints. Runtime sampling maps the domain to `[0,N-1]`, selects `floor(u)`, and linearly interpolates adjacent samples. Identity bypass is exact. Allocation uses checked standard containers; non-finite generated samples fail compilation.

## Identity, serialization, and caches

The stable serialization/cache key is ASCII with schema `rgb-curves:v1`, fixed operation order, FP32 precision, LUT size, and hexadecimal IEEE-754 bits for every enum, domain, enabled flag, point count, and point coordinate. It contains no address or implementation-defined hash. Immutable compiled objects are safe for concurrent reads and copies share ownership. Compilation uses a process-wide synchronized weak cache; expired entries are evicted on inspection and `clear_cache()` supports safe shutdown. A changed point or quality produces a distinct key.

## Execution status and limitations

`CompiledRgbCurves` is the independent CPU reference and `add_rgb_curves_cpu_pass` is an explicit Render Graph CPU-reference node with declared source/destination states. Native Vulkan, D3D12, Metal, GLES binding/dispatch, native LUT resources, preview consumption, provenance extension, and native numerical qualification are **not implemented** in this change. GPU selection therefore cannot invoke curves and must report unsupported; it never silently calls this CPU pass. This deliberately does not claim Preview = Export. Wheels, qualifiers, 3D LUTs, UI, and export work remain future milestones.

## Performance harness

`digitor_rgb_curves_benchmark` is non-gating and covers 1080p/4K, all LUT sizes, cold/warm compilation, cache hits, and a complex curve. It separates coefficient-plus-LUT generation from CPU application. Native pipeline creation, upload, execution, synchronization, and readback cannot truthfully be timed until native execution exists; normal preview timing includes no validation readback.

## Native execution contract (v4.7 development)

The canonical backend shader is `src/gpu/shaders/rgb_curves.hlsl`. It consumes four contiguous FP32 LUT planes (Master, Red, Green, Blue), performs endpoint-inclusive `u=(x-lo)*(N-1)/(hi-lo)` addressing and explicit linear interpolation, preserves unbounded linear extrapolation, and restores alpha. `NativeRgbCurvesKey` includes the complete compiled serialization, LUT size, interpolation and shader ABI versions, precision, backend, and device identity. `NativeRgbCurvesCache` is a bounded 64-entry LRU with synchronized in-flight creation; native ownership remains separate from the weak CPU compilation cache.

The backend-neutral `RGB Curves Native` Render Graph pass declares shader-read/source and shader-write/destination dependencies. It accepts only a native executor and has no CPU fallback callback. At this revision native backend adapters have not yet wired the canonical artifact, so this is not a v4.7.0 release and the version remains 4.6.1.
