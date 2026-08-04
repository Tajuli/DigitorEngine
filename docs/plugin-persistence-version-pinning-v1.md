# Plugin Installation Persistence and Exact Version Pinning v1

DigitorEngine now exposes a host-owned persistence contract for installed filter, effect and transition packages. The Digitor app may store records in SQLite, JSON, secure app storage or another platform-appropriate database without putting filesystem or account policy inside the engine.

## Persistent installation record

Each record contains the plugin ID, exact version, package path, SHA-256 package identity, plugin kind and install state. Restore validates every record and rejects malformed paths, hashes, identities, oversized stores and duplicate plugin IDs before replacing the active in-memory snapshot.

## Project pin

A project references a plugin with:

```text
plugin_id
plugin_version
package_sha256
```

Resolution succeeds only when all three values exactly match a restored installation. A newer catalog or installed update is never substituted silently. Missing installations, version mismatches, hash mismatches, revoked packages and quarantined packages return distinct statuses so the Digitor app can offer exact-version download, migration or replacement.

## Lifecycle

```text
install/update -> persist record -> app restart -> restore records
project open -> resolve exact pin -> preview/export
uninstall -> persist removal
```

The engine does not own subscriptions, purchases, free/paid access or export entitlement. Those remain Digitor app responsibilities.
