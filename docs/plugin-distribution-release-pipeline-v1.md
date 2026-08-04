# Plugin Distribution & Release Pipeline v1

This additive release layer converts validated code-free filter, effect and transition packages into a deterministic website publishing bundle.

It defines exact plugin ID/version/SHA-256 release records, HTTPS download and preview metadata, publisher signatures, revocation state, per-backend artifacts and deterministic catalog signing input. Exact-version import fixtures reject missing, revoked or hash-mismatched packages.

The generated publishing layout contains versioned `.digitorfx` packages plus `catalog/v1/catalog.json` and `catalog/v1/catalog.sig`. Commercial free/paid metadata and export entitlement remain entirely in the Digitor app and website services; DigitorEngine only validates and processes app-authorized packages.

This milestone does not redesign or replace the render graph, shader compiler, shader reflection, pipeline cache, Primary Wheels, backend selection, CPU fallback policy, preview runtime, export runtime or existing plugin algorithms.
