# GPU color pipeline

`Engine::grade_rgba32f` currently executes a direct FP32 native backend pass;
it does not route through the generic callback render graph and never calls
`process_cpu`. Supported stage order is temperature/tint, vibrance/saturation,
contrast, lift/gain/offset, exposure, signed gamma, then hue rotation; alpha is
preserved. See the [v4.6.1 execution audit](grade_rgba32f_execution.md) for
source-level backend chains and truthful qualification state.

RGBA values remain float through the grade buffers/textures, including
negative and over-range values. FP16, tone mapping, arbitrary matrices, curves,
wheels, qualifiers, and LUTs are not part of this entry point.
