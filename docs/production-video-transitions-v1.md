# Unified Transition Subsystem v1

DigitorEngine exposes one transition subsystem for built-in and plugin transitions. The subsystem owns a shared descriptor registry, stable transition IDs, a common `PluginTransitionRequest` GPU surface contract, one dispatcher, and one preview/export policy.

Built-ins are registered as `builtin.cross-dissolve`, `builtin.dip-to-color`, `builtin.wipe`, and `builtin.slide`. Third-party transition descriptors use the same registry but cannot replace or collide with built-in IDs. Timeline and Flutter code select a transition by ID without creating a separate rendering path for plugins.

Built-in CPU reference rendering remains deterministic RGBA32F and produces identical preview/export digests. GPU execution for both provider kinds validates the same outgoing, incoming, and output texture contract. Missing recorders and backend failures are explicit; a selected GPU path never silently falls back to CPU.

The C ABI exposes built-in transition enumeration and packed RGBA32F rendering. The unified qualification workflow builds both the existing code-free plugin runtime and the built-in/plugin dispatcher integration on Ubuntu, Windows, and macOS.
