# Plugin Package Security & Atomic Installation v1

DigitorEngine now provides a hardened host contract for installing downloaded filter, effect and transition packages.

## Validation before extraction

Packages are rejected when identity metadata, publisher signature, archive hash, entry paths, entry counts, expanded size or expansion ratio violate policy. Absolute paths, parent traversal, Windows drive paths, duplicate entries, symbolic links and executable payloads are blocked by default.

## Atomic lifecycle

A package is verified, extracted into a staging location and activated with one host-provided atomic operation. Failed extraction or activation triggers rollback, so an existing working version is not partially replaced.

## Revocation

Revoked packages or publishers are rejected before staging. The Digitor app remains responsible for refreshing the trusted publisher key store and revocation data.

## Commercial policy boundary

The security layer contains no free/paid or entitlement logic. The app decides whether an import or export is authorized; DigitorEngine validates and processes the package safely.
