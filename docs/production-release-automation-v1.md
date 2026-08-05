# Production Release Automation v1

This milestone gates automated release publication across Windows, Android, macOS and iOS.

A candidate is publishable only when the tag matches the semantic version, all four platforms are present, ABI/source metadata is coherent, package names and SHA-256 values are unique, artifacts are non-empty, and every artifact carries SBOM, provenance, consumer-link smoke, exported-symbol verification, signing completion and real-device hardware evidence.

Dry-run validates the same contract without publishing. Missing signing, notarization, store approval or hardware evidence returns `blocked`; it is never reported as production ready.

The engine exposes a stable C ABI so Flutter/release tooling can validate the assembled release before upload. External credentials, certificates, notarization and store submission remain release-environment responsibilities.
