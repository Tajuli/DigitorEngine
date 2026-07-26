# Digitor color pipeline (v3.3–v3.6)

All grading is performed in linear-light floating point. Encoded RGBA/BGRA inputs are
decoded with the IEC 61966-2-1 transfer function. YUV uses the BT.709 matrix
`R=Y+1.5748V`, `G=Y-0.187324U-0.468124V`, `B=Y+1.8556U`; studio range maps
Y from `[16,235]` and chroma from `[16,240]` before conversion. Chroma planes use
4:2:0 cosited nearest reconstruction, including odd image dimensions.

The backend-neutral graph schedules primary correction, component/master curves,
hue-domain curves, and color wheels in that order. Parameters are immutable for a
dispatch and may be serialized into a `ParameterBuffer`. Shader cache identity is
language, stage, and source; pipeline identity is the ordered shader-hash tuple.
Vulkan, D3D12, Metal, and OpenGL ES uploads share the same validated plane contract.

Primary correction applies temperature/tint, saturation, contrast about 0.5,
lift/gain/offset, exposure multiplication by `2^e`, and signed power gamma. Curves
are monotonic-domain piecewise-linear functions. Hue curves operate in cylindrical
HSV coordinates; luminance is `0.2126R + 0.7152G + 0.0722B`. Wheels apply per-channel
signed power: `sign(x+l) |(x+l)g|^(1/gamma)`.

The HSL qualifier forms a matte as the product of hue, saturation, and luminance
trapezoids. Softness controls the linear shoulders. Clean black/white clamp the
tails; inversion complements the matte. Blur is a bounded box convolution and
denoise expands values away from 0.5. Eye-dropper sampling sets centered HSL ranges.
CPU and GPU paths intentionally call the same normative arithmetic; pixel validation
reports maximum error, mean error, and the count exceeding a caller tolerance.

Export is deliberately outside this milestone.
