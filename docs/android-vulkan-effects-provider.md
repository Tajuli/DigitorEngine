# Android Vulkan native effects provider v1

This provider connects the shared `NativeEffectRuntime` to the exact Vulkan device, queue and command pool already owned by the Android production runtime.

## Guarantees

- Vulkan remains the first Android effects backend; OpenGL ES is a later fallback milestone.
- Input and output images belong to the exact same `VkDevice` identity.
- External decoder/preview/encoder images are supplied as zero-copy `VkImage` handles.
- External synchronization is mandatory; the caller must hand images to the provider in `VK_IMAGE_LAYOUT_GENERAL` and must not reuse them until completion.
- Intermediate images use optimal tiling and device-local memory only.
- The provider owns command-buffer reset/begin/end, queue submission, fence wait and failure recovery.
- Blur, glow and motion blur use two provider passes; other built-in effects use one pass.
- The shader callback may bind pipelines and descriptors and record dispatches only. It must not create another device/queue or perform CPU staging/readback.
- If Vulkan is selected, execution fails closed. It does not silently switch to GLES or CPU.

## Build

Include `cmake/AndroidVulkanEffectsProvider.cmake` from the Android native build and call:

```cmake
digitor_enable_android_vulkan_effects_provider(digitor_engine)
```

The final Android host must pass the same physical device, logical device, queue, queue family and command pool used by decode, preview and hardware export.

## Remaining qualification

The repository-owned SPIR-V shader package and physical Android qualification remain required. Release evidence must execute all nine built-in effects on SDR and HDR-capable devices, verify preview/export numerical parity, prove zero readback/re-upload/fallback telemetry, exercise lifecycle/cancellation/memory pressure and test Vulkan-unavailable fallback to the future GLES provider.
