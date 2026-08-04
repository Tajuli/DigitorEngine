# Plugin Update Migration & Missing-Plugin Recovery v1

This additive host-facing layer preserves exact plugin ID, version and SHA-256 project pins while helping Digitor recover missing or obsolete filter, effect and transition packages.

Recovery order is deterministic: use the exact installed package, download the exact pinned package, apply an explicitly declared compatible migration, use an app-selected replacement, or keep the instance safely disabled. The engine never silently substitutes a newer package.

Migration descriptors can rename or remove parameters. Digitor app remains responsible for presenting choices, downloading packages, purchase/subscription state and preview/export authorization.

This milestone does not modify the render graph, shader compiler, shader reflection, pipeline cache, Primary Wheels, backend selection, CPU fallback policy, preview runtime, export runtime or plugin processing algorithms.
