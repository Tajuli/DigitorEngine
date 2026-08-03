# Native GPU Effects Runtime v1

This runtime binds the completed `EffectStack` contract to backend-owned D3D12, Vulkan, Metal, or OpenGL ES textures.

## Zero-copy contract

- Input, output, and transient surfaces share one device identity.
- External preview/export surfaces must expose external-memory identity.
- CPU-mappable surfaces are rejected.
- CPU readback, re-upload, and fallback telemetry remain zero in the runtime.
- NV12/P010 are converted by the existing decode/color pipeline before effects; effects execute on RGB working surfaces.
- HDR execution requires an RGBA16F surface and a provider that explicitly advertises HDR support.

## Multi-pass execution

The backend provider supplies the pass count for each stable effect ID, allocates engine-owned transient textures, records each pass, performs synchronization, submits once, and releases transients after submission.

The runtime caps an effect at 32 passes and rejects incomplete providers, missing synchronization, surface aliasing, geometry mismatches, unknown effects, invalid parameters, and incorrect transient resources.

## Backend assembly

Production providers map the opaque surface handle to:

- D3D12 resource + descriptor/fence state
- Vulkan image/view + external-memory/semaphore state
- Metal texture + command-buffer event state
- GLES texture/EGLImage + native-fence state

The provider owns shader modules, pipeline state, descriptor binding, resource barriers, transient pooling, and device-loss reporting. The runtime owns stack ordering, validation, pass scheduling, and zero-copy telemetry.

## Qualification

Release evidence must show, per backend:

- all built-in effect IDs execute on native textures
- multi-pass blur/glow and stacked effects preserve ordering
- preview and export consume the same `EffectStack`
- SDR and HDR output parity
- alpha preservation
- zero CPU readback/re-upload/fallback counters
- cancellation, device-loss, memory-pressure, and long-run recovery

This PR provides the production native execution boundary. Concrete shader/pipeline providers and physical-device evidence are required before a backend is marked effects-qualified.
