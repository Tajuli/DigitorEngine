# Digitor Remote Filter and Effects Marketplace v1

DigitorEngine supports signed `.digitorfx` filter/effect packages published through an approved website or GitHub-backed catalog. Normal plugins can be added, imported and applied without editing or rebuilding engine source.

## Authority boundary

The consumer app is the only authority for user plans, subscriptions, purchases, trials, preview rights and export rights.

DigitorEngine does **not**:

- identify free or paid users;
- validate subscriptions or purchases;
- contact Google Play, App Store, Stripe or a licensing server;
- decide whether a paid plugin may preview or export;
- show upgrade UI.

The catalog `tier` and `product_id` fields are metadata for the app. For each browse, import, preview, export or remove operation, the app returns an authorization decision through `ConsumerPluginAuthorize`. When the app allows the operation, the engine continues. When the app denies it, the engine stops before invoking the apply/export callback and returns the app-provided diagnostic.

Example Digitor app policy:

```text
Free plugin + any user:
  import = allow
  preview = allow
  export = allow

Paid plugin + free user:
  import = allow
  preview = allow
  export = deny ("upgrade required")

Paid plugin + subscribed user:
  import = allow
  preview = allow
  export = allow
```

## Runtime flow

```text
Approved website / GitHub catalog
  -> signed catalog validation
  -> backend artifact selection
  -> SHA-256 verification
  -> atomic package installation
  -> runtime registration
  -> consumer app authorization
  -> preview or export apply callback
```

The engine still enforces technical and security requirements: trusted catalog signatures, package hashes, engine/backend compatibility, parameter ranges, revocation, exact installed version pinning, package limits and prohibition of downloaded native executables.

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
  -> update catalog
  -> Digitor app discovers/imports package
  -> app authorizes preview/export
  -> engine applies plugin
```

No engine source change or app release is required for a normal plugin that stays within the generic single/multi-pass runtime contract.
