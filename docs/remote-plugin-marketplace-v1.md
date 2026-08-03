# Digitor Remote Filter and Effects Marketplace v1

This contract allows Digitor users to import filters and effects from an approved website or GitHub-backed catalog without changing, rebuilding, or rereleasing DigitorEngine for each plugin.

## Trust boundary

DigitorEngine never executes downloaded native DLL, SO, dylib, or arbitrary host code. A package contains a declarative manifest, parameter schema, assets, and backend shader artifacts only. The app supplies HTTPS transport, trusted publisher keys, purchase entitlement verification, atomic storage, and the runtime shader registrar.

## Catalog flow

1. The app downloads the signed catalog from the configured Digitor website.
2. The engine validates schema limits, IDs, versions, publisher signatures, backend coverage, package paths, and revocation state.
3. The user selects a free or paid plugin.
4. Paid plugins require a host-verified entitlement token tied to the catalog product ID.
5. The selected backend package is downloaded and SHA-256 verified.
6. The host atomically installs the package into staging and returns the final isolated path.
7. The engine registers the filter/effect dynamically through the existing plugin/effect runtime.
8. Preview and export resolve the exact installed plugin version and package hash.

## Package layout

```text
plugin-id-1.2.0.digitorfx/
  manifest.json
  parameters.json
  shaders/windows-d3d12.dxil
  shaders/windows-vulkan-rgba8.spv
  shaders/windows-vulkan-rgba16f.spv
  shaders/android-vulkan-rgba8.spv
  shaders/android-vulkan-rgba16f.spv
  shaders/android-gles.frag
  shaders/apple.metallib
  assets/thumbnail.webp
```

## Free and paid plugins

Free entries use `tier=free` and install after signature/hash verification. Paid entries use `tier=paid`, require a stable `product_id`, and cannot download/install/register unless the host entitlement callback succeeds. Payment-provider secrets and store receipts never enter shader packages.

## Required website endpoints

The host may use any domain or GitHub Releases. The engine only requires callbacks, so URLs are not hardcoded.

- stable catalog JSON
- beta catalog JSON (optional)
- immutable versioned package URLs
- publisher public-key/revocation metadata
- entitlement endpoint for paid products

## Update, rollback, revocation

Catalog version changes mark installed plugins as update available. Installation must be atomic: download to staging, validate, register, then swap. The app retains the previous package until the new version is proven usable. Revoked packages are marked `revoked`, removed from availability, blocked from new registration, and must be disabled by the host UI/runtime.

## Security limits

- maximum 4096 catalog entries
- maximum 128 parameters per plugin
- maximum 8 backend artifacts per plugin
- maximum 256 MiB downloaded package
- no parent-directory package paths
- mandatory publisher signature and SHA-256
- no network/filesystem permission inside shader plugins
- no post-selection backend or CPU fallback

## Future publishing workflow

A publisher adds a new signed `.digitorfx` package and catalog entry to the approved website or GitHub release. The app refreshes the catalog and users can install it immediately. No DigitorEngine source change is required unless the plugin needs a new resource type or execution capability outside the generic runtime contract.
