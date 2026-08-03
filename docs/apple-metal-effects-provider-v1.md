# Apple Metal Native Effects Provider v1

This package binds `NativeEffectRuntime` to the existing macOS/iOS Metal device and command queue without creating a second GPU device or CPU staging path.

## Repository-owned responsibilities

- shared `MTLDevice` and `MTLCommandQueue` identity validation
- private transient `MTLTexture` allocation
- RGBA8, BGRA8 and RGBA16F working surfaces
- single-pass and multi-pass effect scheduling
- command-buffer submission and completion validation
- transient lifetime retention until GPU completion
- fail-closed handling for device mismatch, invalid formats and dispatch failure
- zero CPU readback, re-upload and fallback contract

## Shader package boundary

The provider accepts a stable shader-package identity and a dispatch callback. The callback records the effect-specific Metal compute encoder and pipeline state into the provider-owned command buffer. It must not create another queue, map textures to CPU memory or perform a fallback dispatch.

## Build integration

Include `cmake/AppleMetalEffectsProvider.cmake` and call `digitor_configure_apple_metal_effects_provider(<target>)` for the Apple release target.

## Remaining qualification

The repository-owned MSL shader package and real Mac/iPhone qualification remain required. Physical evidence must cover all built-in effects, SDR/HDR, alpha preservation, preview/export identity, zero-copy telemetry, lifecycle interruption and long-run stability.
