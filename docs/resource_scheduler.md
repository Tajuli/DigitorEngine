# Resource scheduler

The resource scheduler is a compile-time component of `RenderGraph`; backend implementations do not
duplicate lifetime or hazard logic.

## Lifetime and allocation

First and last use are measured in final schedule coordinates, not insertion coordinates. Unused
resources have an invalid first pass and are not placed in the transient pool. The allocator visits
resources by stable resource ID and selects the lowest compatible slot. A slot is reusable only when
the prior lifetime is disjoint, its allocation is large enough, and its resource type matches.
Imported/persistent resources retain unique storage.

An application can declare an early logical destruction point with `release_resource`. Any live use
after that point fails compilation, detecting read-after-free before command recording.

## Hazards and synchronization

For each resource the compiler tracks the last writer and readers since that write. It emits edges
for read-after-write, write-after-read, and write-after-write hazards. Explicit edges represent
non-resource ordering. A deterministic topological sort detects cycles. State tracking begins at an
import's declared state (or undefined for transients) and emits a transition whenever the next use
requires another state. Thus native backends receive one schedule and one explicit barrier plan;
they only translate those transitions to Vulkan barriers, D3D12 barriers, Metal encoder boundaries,
GLES memory barriers, or the CPU validation command.

The schedule exposes a queue index and dependency list, but v4.4 intentionally submits queue zero
serially. No asynchronous execution is enabled in this milestone.

## Caches

`RenderPipelineCaches` owns the shader, pipeline-state, descriptor-layout, and sampler caches. Keys
are content based and handles use stable hashing, so creation order and process address layout do not
affect graph replay. Native objects can be attached behind these stable cache identities without
changing graph construction or the C ABI.
