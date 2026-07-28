# Production color-science foundation

The single normative implementation is `include/digitor/color_science.hpp` plus
`src/gpu/color_science.cpp`. Preview and export must compile the same immutable
`TransformGraph`; copying formulas into either path is prohibited.

Implemented CPU reference operations are transfer encode/decode, matrix derivation
and inversion, RGB/XYZ and RGB/RGB conversion, Bradford adaptation, YUV code-value
decoding, graph validation/execution, and baseline tone mapping. GPU compilation
of this graph is **not implemented** in v4.5.0: no backend is represented as
verified and GPU selection must report unsupported rather than call this CPU path.
ACES, display mapping, gamut compression, HDR-to-SDR and SDR-to-HDR are not
implemented. PQ/HLG transfer support alone is not a claim of an HDR pipeline.

## Mathematical sources

Chromaticities and YUV coefficients are from ITU-R BT.601, BT.709 and BT.2020;
sRGB constants are from IEC 61966-2-1; PQ constants are from SMPTE ST 2084;
HLG constants are from ARIB STD-B67/ITU-R BT.2100; Bradford uses the published
cone-response matrix in `color_science.cpp`. Tests use independently stated
standard code values and published D65 matrices rather than implementation output.
