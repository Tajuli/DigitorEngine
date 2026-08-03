# Unified Visual Stack v1

`VisualStack` is the project-level contract that binds the completed filter, plugin/beauty, and effects subsystems to one clip payload.

Processing order is fixed and versioned:

1. filter stack
2. plugin and beauty instances
3. effect stack

Preview and export must deserialize and validate the same payload. The format uses length-prefixed embedded subsystem payloads so plugin/filter/effect serialization can evolve without delimiter ambiguity.

Validation fails closed for malformed payloads, excessive section sizes, unknown plugins, invalid plugin parameters, unknown effects, and invalid effect ranges. Plugin count is capped at 256 and each embedded section is capped at 16 MiB.

This milestone does not claim native backend multi-pass effect shaders or zero-copy surface execution. It provides the stable project/clip persistence boundary required before native backend execution is attached.
