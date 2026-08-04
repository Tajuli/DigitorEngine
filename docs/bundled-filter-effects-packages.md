# Bundled filter and effect packages

DigitorEngine treats built-in, website-hosted and GitHub-hosted filters/effects as the same signed `.digitorfx` package type.

## Permanent engine architecture

```text
Digitor app bundle / approved website / GitHub release
  -> catalog entry
  -> signed .digitorfx package
  -> hash and signature validation
  -> backend artifact selection
  -> generic pipeline loader
  -> generic single/multi-pass zero-copy runtime
  -> preview/export
```

The engine does not require or permit per-effect edits to:

- C++ enums;
- effect-ID switches;
- registry-size constants;
- D3D12, Vulkan, Metal or GLES source mappings;
- preview/export code;
- commercial free/paid policy.

## Adding a built-in effect

1. Create the package manifest and parameter schema.
2. Add precompiled backend assets for D3D12, Vulkan, Metal and GLES.
3. Sign the package and catalog entry.
4. Put the package in the Digitor app bundle.
5. Add its entry to the bundled catalog.

No DigitorEngine source change or rebuild is required. A Digitor app release is required only when the package is physically bundled with the app. Publishing the same package through the approved remote catalog does not require an app release.

## Adding a remote effect

Upload the signed package to the approved website or GitHub release and add its catalog entry. Digitor imports and registers it through the existing marketplace/runtime.

## Built-in versus remote

`bundle://` is only a transport location. It does not create a different execution path. Bundled and remote packages use identical package validation, backend selection, native GPU pipeline creation, zero-copy execution, color metadata, alpha policy and preview/export parity gates.

## Commercial policy

DigitorEngine does not know whether a package or user is free or paid. The Digitor app decides whether to submit preview or export requests. The engine processes every valid request identically.
