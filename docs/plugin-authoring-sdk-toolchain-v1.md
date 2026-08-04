# Plugin Authoring SDK & Package Toolchain v1

This milestone gives Digitor and third-party authors a code-free package-authoring contract for filters, effects and video transitions. Authors describe identity, localized metadata, typed parameters and backend shader artifacts, then generate a deterministic `.digitorfx` package plan and signing payload without editing DigitorEngine source.

The validator enforces stable plugin IDs and versions, unique locales and parameters, finite ranges, per-backend artifacts, lowercase SHA-256 identities, safe relative paths, deterministic file ordering, package size limits, alpha preservation and deterministic processing.

The included `digitor_plugin_author --self-test` CLI demonstrates reference transition packaging, while the qualification builds reference filter, effect and transition manifests for D3D12, Vulkan, Metal and OpenGL ES.

Commercial policy remains outside the engine. The Digitor app controls free/paid import, preview and export authorization. This additive authoring layer does not change the render graph, shader compiler, shader reflection, pipeline cache, Primary Wheels, backend selection, CPU fallback policy, preview runtime, export runtime or plugin processing algorithms.
