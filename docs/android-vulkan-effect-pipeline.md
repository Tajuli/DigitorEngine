# Android Vulkan built-in effect pipeline v1

This package binds the repository-owned SDR and HDR SPIR-V variants to the merged Android Vulkan native effects provider.

## Runtime ownership

The package creates and retains the exact-device Vulkan objects required by the built-in effects path:

- SDR and HDR `VkShaderModule` objects;
- storage-image descriptor-set layout and descriptor pool;
- push-constant pipeline layout;
- one compute pipeline per storage-image format class;
- per-pass input/output image views and descriptor sets;
- compute-to-compute image barriers;
- stable effect-ID to shader constant mapping.

It records into the provider-owned command buffer. It does not create another queue, map an image to CPU memory, stage pixels through host memory, read pixels back, or silently fall back to GLES/CPU after Vulkan selection.

## Surface contract

External decoder, preview and export images must:

- belong to the exact provider `VkDevice`;
- use `VK_IMAGE_LAYOUT_GENERAL` when passed to the effect runtime;
- support storage-image access in their declared format;
- be externally synchronized by the platform integration;
- remain alive through the provider's synchronous fence completion.

Supported working formats are RGBA8, BGRA8 and RGBA16F. RGBA16F selects the HDR pipeline; RGBA8/BGRA8 select the SDR pipeline.

## Qualification boundary

The hosted CI gate cross-compiles the generated SPIR-V, provider and pipeline package with the Android NDK for arm64-v8a. It is a source/package qualification gate, not physical-device evidence.

Final Android Vulkan release sign-off still requires a real supported Android device to execute all nine effects on decoder-backed and encoder-bound images, verify numerical output and preview/export identity, exercise cancellation/lifecycle/device-loss paths, and prove zero readback/re-upload/fallback telemetry on the exact release commit.
