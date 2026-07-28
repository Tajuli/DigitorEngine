# Primary Color Wheels development specification

## Mathematical contract

Primary Wheels operate on unpremultiplied, scene-linear FP32 RGB. For channel
`c`, canonical RGB values `L`, `G`, `A`, `O`, and linked masters `Lm`, `Gm`,
`Am`, `Om`, the immutable order is:

1. Lift: `c = c + L + Lm`.
2. Gamma: `c = sign(c) * pow(abs(c), 1 / (G * Gm))`.
3. Gain: `c = c * G * Am`.
4. Offset: `c = c + O + Om`.

Disabled stages are skipped. Gamma's reference/pivot is 1.0: zero remains zero
and one remains one. There is no implicit clamp. Finite negative and over-range
values therefore remain representable. A non-finite input channel is propagated
unchanged; finite parameters cannot introduce infinity within their declared
ranges for qualified inputs. Alpha is copied without arithmetic, including a
non-finite alpha. FP32 with contraction/fast-math disabled is the qualification
mode. Primary Wheels precede RGB Curves in the canonical production chain;
reverse ordering is permitted only as an explicitly different comparison graph.

## Parameters and wheel-vector contract

Lift and Offset RGB/master values are additive in `[-4,4]`. Gamma RGB/master
values are factors in `[0.01,10]`; Gain RGB/master values are factors in
`[0,16]`. Defaults are respectively 0, 1, 1, and 0. All values must be finite.
The schema version is 1. Serialization contains version, each IEEE-754 binary32
bit pattern in fixed little-endian order, then enable flags; it is also the
deterministic cache identity. Instances are immutable and shareable.

UI coordinates are deliberately not accepted by the engine. A conforming wheel
adapter maps hue angle zero to red and increasing angles through green then blue.
For normalized radius `r` in `[0,1]`, it evaluates the unit RGB hue vector `h`,
then submits `r * M * (h - dot(h, (0.2126,0.7152,0.0722)))`, where `M` is no
greater than the applicable RGB parameter range. Thus the wheel vector has zero
Rec.709 luminance; center is exactly neutral. The linked master is separate and
is added (Lift/Offset) or multiplied (Gamma/Gain) as specified above. This is an
adapter contract, not a gesture or UI implementation.

## Execution, graph, preview, and cache contract

`primary_wheels.hlsl` is the sole backend shader mathematics. Native backends
must compile it to SPIR-V, DXIL, Metal, or GLSL ES artifacts through the existing
compiler paths, bind source/destination RGBA32F resources and the immutable
parameter block, and return `UNSUPPORTED` when required capabilities are absent.
They may not call the CPU reference or silently fall back. Render Graph passes
declare source read and destination write states, making replay, barriers,
lifetime, culling, and parameter-identity invalidation explicit. Normal preview
must retain a `ProcessedGpuFrame`; readback is validation-only.

Fixed qualification tolerances are maximum absolute error `2e-5`, maximum
relative error `2e-5` (for reference magnitude above `1e-6`), RMS `5e-6`, PSNR
at least 100 dB, SSIM at least 0.99999, and zero pixels outside the absolute and
relative limits. Reports also include first failure, worst pixel, backend,
device, and dimensions. CPU timing is never reported as GPU performance.

## Implementation truth table

The project remains at 4.7.0 until every v4.8.0 qualification gate is met. Native
texture execution is implemented for Vulkan, D3D12, Metal, and GLES, but no
hardware runner was available in this development environment; these paths are
therefore hardware-unverified rather than runtime-qualified.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Direct GPU Preview | Evidence | Numerical Result | Status |
|---|---|---|---|---|---|---|---|---|---|
| Lift RGB | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | CPU exact cases pass; GPU not run | Implemented, hardware-unverified |
| Lift master | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| Gamma RGB | signed power FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | CPU `1e-6`; GPU not run | Implemented, hardware-unverified |
| Gamma master | signed power FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| Gain RGB | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| Gain master | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| Offset RGB | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| Offset master | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| All controls combined | FP32 | native compute | native compute | native compute | native draw | GPU-only | backend sources | GPU not run | Implemented, hardware-unverified |
| Negative values | preserved | signed pow | signed pow | signed pow | signed pow | yes | shader sources | GPU not run | Implemented, hardware-unverified |
| Over-range values | preserved | preserved | preserved | preserved | preserved | yes | shader sources | GPU not run | Implemented, hardware-unverified |
| Alpha preservation | bit copy | shader copy | shader copy | shader copy | shader copy | yes | shader sources | GPU not run | Implemented, hardware-unverified |
| Render Graph integration | CPU pass | native pass | native pass | native pass | native pass | exported pass | renderer/graph tests | CPU schedule passes; GPU not run | Implemented, hardware-unverified |
| RGB Curves interoperability | ordered graph contract | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | specification | not GPU-qualified | Interface/design only |
| ProcessedGpuFrame output | N/A | VkImage owner | ID3D12Resource owner | MTLTexture owner | GL texture owner | yes | backend sources | not run | Implemented, hardware-unverified |
| Direct preview | N/A | GPU copy | GPU copy | GPU blit | texture sample | yes | existing generic presenter | no normal readback | Implemented, hardware-unverified |
| Failure injection | CPU validation | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | provenance fields | not run | Interface/design only |
| FP32 | reference | RGBA32F | RGBA32F | RGBA32Float | RGBA32F | yes | backend sources | CPU only; GPU not run | Implemented, hardware-unverified |
| FP16 | Unsupported | Unsupported | Unsupported | Unsupported | Unsupported | Unsupported | specification | N/A | Unsupported |

Non-gating performance cases are 1920×1080 and 3840×2160, one/all controls,
warm parameter/pipeline cache, and Primary Wheels followed by RGB Curves. A
hardware report must separate upload, dispatch, synchronization, and validation
readback. No GPU measurements or CI runtime results are asserted by this change.
The public C ABI is unchanged. Known limitation: native hardware qualification
and platform CI evidence must be supplied by their respective runners.
