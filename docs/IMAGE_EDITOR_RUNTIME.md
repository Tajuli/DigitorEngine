# DigitorEngine Image Editor Runtime

This document defines the production contract for using the existing DigitorEngine video node, color, filter and effect pipeline with JPEG, PNG and WebP images.

## One runtime

Applications should use `digitor::ImageEditorRuntime` rather than manually combining `GpuImageSession`, `StillImageAsset`, cache revisions and export calls.

The runtime owns one image session and locks one execution backend for its lifetime:

- A complete GPU host selects GPU. Decode/upload, resize, node processing, preview and export remain GPU-only. A GPU failure is returned to the application and never silently switches to CPU.
- When no usable GPU host exists and CPU fallback is enabled, the runtime opens the existing CPU image provider and runs the same logical node order, parameters, alpha and color-management rules on CPU.
- Preview and export receive the same graph and parameter revisions.

## Existing video processing only

The image editor does not define a second filter or effect system. The host callbacks must invoke the same production executor used by video for:

- correction controls
- primary and log wheels
- RGB curves
- HSL qualifier
- LUT/filter nodes
- masks
- serial and parallel node graphs
- blur, sharpen, glow, grain, vignette and every other supported video effect

## Application flow

1. Select a JPEG, PNG or WebP image.
2. Construct the platform GPU host. If the platform cannot establish a usable GPU backend, provide the production CPU node executor.
3. Call `ImageEditorRuntime::open`.
4. Bind or mutate the existing video node graph.
5. Increment graph revision for structural changes and parameter revision for value changes.
6. Call `render_preview` and present the returned native GPU frame or CPU frame according to the locked backend.
7. Call `export_image` for full-resolution JPEG, PNG or WebP output.

## Pixel-accuracy requirements

Preview and export must share:

- node order and graph topology
- shader artifacts or CPU reference operations
- parameter buffers and interpolation
- working color space and transfer functions
- alpha convention
- sampling kernel and edge behavior
- precision and rounding policy

JPEG export must explicitly flatten alpha. PNG and WebP may preserve alpha. EXIF orientation must be applied before graph processing. Color metadata must remain stable from decode through export.

## Production qualification gate

The image editor is production-qualified only after all of the following pass on Windows, Android, macOS and iOS where applicable:

- JPEG, PNG and WebP decode fixtures
- all eight EXIF orientations
- opaque and transparent alpha fixtures
- GPU-only execution when a GPU host is selected
- CPU-only execution when no usable GPU exists
- no mid-session GPU-to-CPU fallback
- preview/export graph and parameter revision identity
- original-resolution export
- large-image memory and tiling limits
- cancellation and progress callbacks
- stale/destroyed FFI handle rejection
- per-pixel maximum-error and RMS thresholds

Physical-device captures remain necessary for final Vulkan, D3D12, Metal and GLES qualification.
