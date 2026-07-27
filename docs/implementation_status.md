# Implementation status — v4.4.0

## Production render graph and resource scheduler

The backend-neutral render graph now implements deterministic pass/dependency graphs, hazard edges,
stable topological ordering, pass culling, explicit transition plans, typed resources, lifetime
validation, and compatible transient aliasing. The schedule carries dependencies and a reserved
queue index for async readiness, but execution remains deliberately serial.

| Capability | v4.4 status |
|---|---|
| Pass and dependency graph | Implemented and host-tested |
| Deterministic topological schedule and graph hash | Implemented and host-tested |
| Texture/buffer/uniform/storage/render-target/readback types | Implemented |
| Lifetime tracking and transient aliasing | Implemented and host-tested |
| Automatic pure-pass culling | Implemented and host-tested |
| RAW/WAR/WAW hazards and explicit barriers | Implemented and host-tested |
| Cycle, duplicate-write, read-before-write/use-after-release validation | Implemented and host-tested |
| Shader/pipeline/descriptor/sampler caches | Implemented as backend-neutral stable identities |
| CPU reference execution of the same graph | Implemented |
| Vulkan/D3D12/Metal/GLES schedule abstraction | Shared; native translation remains backend-owned |
| Async execution | Intentionally disabled; metadata only |

Tests cover replay descriptions, stable hashing, deterministic ordering, resource reuse, pass
culling, multiple resolutions, validation failures, transition generation, and a 512-pass stress
chain. Hardware-specific synchronization still requires qualification on the corresponding native
CI runner; the portable host test does not claim hardware execution.

No C declarations or C structure layouts changed in v4.4. No new color operation, LUT, Curves,
Qualifier, or Timeline work is part of this milestone.

See [render_graph.md](render_graph.md) and [resource_scheduler.md](resource_scheduler.md) for the
construction, validation, allocation, and synchronization contracts.
