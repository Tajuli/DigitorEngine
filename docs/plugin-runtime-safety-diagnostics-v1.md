# Plugin Runtime Safety, Resource Budgets and Diagnostics v1

This additive host-facing layer validates filter, effect and transition workloads before existing GPU processing begins. It provides configurable pass, dispatch, transient texture, transient buffer, parameter and surface deadline budgets; host cancellation; structured preview/export diagnostics; and per-version disable/recovery state after repeated backend failures.

The Digitor app remains responsible for purchase, subscription and export authorization. Free users may import and preview paid plugins, while the app does not submit unauthorized paid export work.

This milestone does not redesign or replace the Render Graph, Shader Compiler, Shader Reflection, Pipeline Cache, Primary Wheels, backend selection, CPU fallback policy, preview runtime, export runtime or existing plugin algorithms.
