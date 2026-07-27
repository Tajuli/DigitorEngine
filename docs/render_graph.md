# Render graph

DigitorEngine 4.4 uses one backend-neutral graph for CPU, Vulkan, Direct3D 12, Metal, and OpenGL
ES work. A pass declares every resource read and write plus the state required by that access. The
graph infers RAW, WAR, and WAW dependencies, combines them with explicit dependencies, and uses a
stable lowest-pass-index Kahn topological sort. Consequently the same graph definition produces the
same schedule and 64-bit FNV-1a graph hash on every backend.

Resources are typed as textures, buffers, uniform buffers, storage buffers, render targets, or
readback allocations. Imported resources supply an initial state; transient resources must be
written before they are read. Exported resources and passes marked `side_effect` are observable
roots. Compilation walks backwards from those roots and culls other passes. `side_effect` defaults
to true to preserve existing graph-builder behaviour; new pure passes should set it false.

Compilation rejects invalid handles and states, duplicate access/writes, transient reads before a
write, explicit/inferred cycles, and use after an explicit release. It then records first/last use in
scheduled-pass coordinates and first-fit aliases compatible transient allocations whose lifetimes do
not overlap. Different resource types never alias.

Each scheduled pass has sorted dependencies, a queue index (currently zero), and the exact barrier
from the previous resource state to its declared state. Queue metadata makes the representation
ready for future asynchronous execution without changing today's deterministic serial semantics.
`replay_description()` provides a stable diagnostic serialization; callbacks are deliberately not
serialized.

## Typical construction

1. Create or import resources with immutable descriptors.
2. Add passes with complete read/write state declarations.
3. Export final resources (or mark externally visible passes as side effects).
4. Compile, inspect validation/schedule data if desired, and execute through `CommandQueue`.

Graph mutation invalidates compilation. Execution recompiles automatically and records all barriers
and pass callbacks into one command buffer. This is also how the CPU backend remains the reference
implementation of the identical scheduling abstraction.

## RGB Curves

`add_rgb_curves_cpu_pass` records an immutable compiled descriptor as an explicit shader-read source and shader-write destination pass. It is pure/cullable, replayable, and participates in normal dependency, barrier, lifetime, and transient-alias analysis. Native GPU curve attachment is currently unsupported rather than hidden outside the graph.
