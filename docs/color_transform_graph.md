# Color transform graph

`TransformGraph::compile` copies stages, validates their canonical enum order,
rejects non-finite parameters, and freezes them. Identity is FNV-1a over explicit
stage tags and IEEE-754 double parameter bytes; it is stable for the same ABI,
serialization and byte order (cross-endian serialization is not yet defined).
The CPU executor implements transfer, matrix/adaptation/working/output conversion,
and tone-map stages without modifying alpha. YUV and future operation execution
must be added before those stages can compile into an executable graph.

Canonical order is YUV conversion, transfer decode, adaptation, source-primary
conversion, working conversion, color operation, output-primary conversion,
transfer encode, tone map. GPU/render-graph compilation is not implemented and
must return an error rather than silently falling back.

Future RGB Curves, Primary Wheels, Log Wheels, HSL Qualifier and LUT contracts
require linear-BT.709 input/output, color-operation placement, preserved alpha,
deterministic versioned serialization/cache contribution, and measured CPU/GPU
parity. Parameter-domain strings live in `future_tool_contract`; the operations
are intentionally unsupported, not placeholders.
