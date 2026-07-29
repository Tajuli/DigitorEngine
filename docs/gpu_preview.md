# Native GPU preview

`ProcessedGpuFrame` is the backend-neutral ownership boundary for a processed
RGBA32F frame. It carries dimensions, timestamp, color and alpha metadata,
backend identity, a stable diagnostic identity, completion state, and an opaque
RAII owner. No Vulkan, D3D12, Metal, or GLES handle is public.

`IRenderBackend::process_curves_gpu` is the live path. Its contract clears the
output on failure and rejects an otherwise successful result if CPU curve work,
fallback, or readback occurred. `present_gpu_frame` is the only live-preview
consumer entry point and records `preview_source = GPU`.

`curves_rgba32f` remains the explicitly CPU-visible validation/export API. A
backend may read back there and records that fact as validation provenance. It
must never be called by `SharedRenderer::render_gpu_preview`.

Metal implements an RGBA32F texture compute pass and a GPU-only blit into its
preview texture. GLES retains the RGBA32F framebuffer attachment and samples it
directly into the current preview framebuffer. Neither path reads curve pixels
on the CPU. The Metal blit is the single unavoidable platform integration copy.

Vulkan writes an RGBA32F storage image and performs a GPU copy into a retained
preview image. D3D12 writes an RGBA32F UAV texture and performs a GPU copy into
a retained preview texture. These are the documented platform-integration
copies; neither reuses the validation buffer/readback path.
