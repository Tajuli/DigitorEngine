# Data-driven video transitions v1

Video transitions use the same signed `.digitorfx` package ecosystem as filters and effects. The engine core does not contain transition algorithm enums, transition ID switches, fixed registry counts, or per-transition backend source mappings.

## Runtime contract

A transition request contains:

- exact plugin ID and version;
- outgoing native GPU texture;
- incoming native GPU texture;
- distinct output native GPU texture;
- normalized progress in `[0, 1]`;
- package parameters;
- selected backend/device identity;
- visual-stack digest and timeline-boundary identity.

All three textures must use the same selected backend, physical device, dimensions and pixel format. The runtime records GPU work only. It performs no CPU readback, CPU upload or silent fallback.

## Adding a transition

Create a signed package containing its manifest, parameter schema, generic pass graph and backend shader assets:

- D3D12 DXIL;
- Vulkan SPIR-V;
- Apple metallib;
- OpenGL ES GLSL.

Publish it in a bundled or remote catalog. No engine source edit is required. Built-in and downloadable transitions execute through the same package, pipeline and zero-copy runtime.

## Planned production milestones

1. Generic two-input transition request and validation.
2. Package manifest/pass graph support for `outgoing`, `incoming`, `output` and `progress` bindings.
3. Timeline overlap scheduling and frame-time calculation.
4. Engine-owned multi-pass GPU intermediates.
5. D3D12/Vulkan/Metal/GLES concrete provider qualification.
6. Preview/export per-pixel SDR/HDR parity, alpha preservation, device-loss and soak qualification.

No commercial free/paid policy belongs in DigitorEngine. The Digitor app decides which preview/export requests to submit.
