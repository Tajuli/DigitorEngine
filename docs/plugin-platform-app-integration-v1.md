# Plugin Platform App Integration v1

This larger additive milestone gives Digitor's Flutter/Dart host one stable pure-C lifecycle for built-in and website-imported filters, effects and transitions.

The host can browse localized catalog items, evaluate compatibility, install, update, uninstall, list installed packages, recover an exact project-pinned ID/version/SHA-256 package and submit processing requests. All callbacks are exception-contained and all returned records are validated before crossing the ABI.

Commercial policy is intentionally absent. The Digitor app may let every user import and preview free or paid packages, while refusing to submit paid export work for a free user. DigitorEngine only processes requests that the app authorizes.

The lifecycle preserves the existing signed package, atomic installation, persistence, exact pinning, migration/recovery, unified runtime and preview/export parity layers. It does not redesign or replace the render graph, shader compiler, shader reflection, pipeline cache, Primary Wheels, backend selection, CPU fallback policy, preview runtime, export runtime or plugin algorithms.
