# Plugin recovery invariants

- Engine source is not edited to add individual filters, effects or transitions.
- Built-in and imported packages use the same plugin execution platform.
- Digitor app owns free/paid, purchase, preview and export authorization.
- Exact project plugin identity is plugin ID + version + SHA-256.
- A newer package is never substituted silently.
- Missing or incompatible instances remain disabled until the app resolves them.
- Main rendering features and backend fallback policy are unchanged.
