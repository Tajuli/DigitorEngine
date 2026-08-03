# Digitor Remote Filter and Effects Marketplace v2

DigitorEngine supports signed `.digitorfx` filter/effect packages published through an approved website or GitHub-backed catalog. Normal plugins can be added, imported and applied without editing or rebuilding engine source.

## Strict authority boundary

DigitorEngine has no concept of free or paid plugins, free or paid users, subscriptions, purchases, trials, preview rights or export rights.

The engine catalog and runtime contain no:

- `tier` field;
- `product_id` field;
- subscription or purchase verifier;
- preview/export authorization callback;
- upgrade-dialog logic.

The Digitor app owns all commercial metadata and policy outside DigitorEngine.

## Requested application policy

The app may implement this behavior:

```text
Free user + commercially paid plugin:
  import plugin
  send full preview requests to engine
  show a small upgrade dialog in app UI
  do not send an export request to engine

Subscribed user:
  send preview requests to engine
  send export requests to engine
```

From the engine's perspective these are ordinary plugin requests. If the app sends a valid preview or export request, the engine processes it normally. If the app sends no export request, the engine performs no export work.

The small preview upgrade dialog is an app overlay. It is not rendered by DigitorEngine and does not modify the plugin output.

## Runtime flow

```text
Approved website / GitHub catalog
  -> signed catalog validation
  -> backend artifact selection
  -> SHA-256 verification
  -> atomic package installation
  -> runtime registration
  -> app submits preview/export request
  -> engine processes request normally
```

The engine continues to enforce only technical and security requirements:

- trusted publisher signatures;
- package hashes;
- engine/backend compatibility;
- parameter ranges;
- revocation;
- exact installed-version pinning;
- package size/path limits;
- no downloaded native DLL/SO/dylib execution;
- no silent backend or CPU fallback after backend selection.

## Catalog schema v2

Schema v2 intentionally excludes all commercial fields. A catalog entry contains identity, version, kind, publisher trust, parameter declarations and backend artifacts only.

Commercial labels, prices, purchase IDs and subscription requirements belong in the Digitor app's own marketplace/catalog model and may reference the engine plugin ID.

## Package layout

```text
plugin-id-1.2.0.digitorfx/
  manifest.json
  parameters.json
  shaders/
    windows-d3d12.dxil
    windows-vulkan-rgba8.spv
    windows-vulkan-rgba16f.spv
    android-vulkan-rgba8.spv
    android-vulkan-rgba16f.spv
    android-gles.frag
    apple.metallib
  assets/
    thumbnail.webp
```

## Publishing workflow

```text
Create shader + declarative manifest
  -> build backend artifacts
  -> sign package/catalog entry
  -> upload to approved website or GitHub Release
  -> update engine plugin catalog
  -> update app commercial metadata when needed
  -> Digitor app discovers/imports package
  -> app decides which requests to submit
  -> engine applies plugin
```

No engine source change is required for a normal plugin that stays within the generic single/multi-pass runtime contract.
