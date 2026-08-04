# Code-free transition plugins

DigitorEngine treats transitions as first-class remote plugins alongside filters and effects.

A transition package can be published to the Digitor website catalog, discovered by kind, signature-verified, downloaded, hash-verified, installed, registered and dispatched without editing or rebuilding engine source for each new transition.

## Package execution contract

Each GPU transition pass declares these bindings:

- `outgoing`: read-only outgoing clip frame
- `incoming`: read-only incoming clip frame
- `output`: writable output frame
- `progress`: read-only normalized transition progress

The program may contain one or more deterministic, alpha-preserving GPU passes. Backend-specific artifacts are selected through the existing marketplace backend contract.

## App and engine responsibilities

The Digitor app owns catalog presentation, free/paid metadata, purchase state, preview permission and export permission. The engine owns package validation, installation, runtime registration and frame processing.

A free user may therefore import and preview a paid transition while the app withholds the export request until entitlement is granted. DigitorEngine contains no subscription or purchase logic.

## Lifecycle

```text
Website catalog
  -> transition discovery
  -> signature and hash verification
  -> package installation
  -> GPU program registration
  -> arbitrary two-input transition dispatch
  -> update or uninstall
```

The cross-platform qualification target verifies this complete lifecycle and proves that a newly published transition ID requires no engine source edit.
