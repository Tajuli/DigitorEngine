# Production Transform & Crop v1

DigitorEngine owns transform, crop and resize pixel generation for preview and export. The subsystem supports normalized crop rectangles, 3x3 affine/perspective mapping, nearest or bilinear filtering, transparent/clamp/mirror edge modes, arbitrary output dimensions, deterministic RGBA32F reference processing and validated Vulkan, Direct3D 12, Metal and OpenGL ES dispatch packets.

Preview and export use the same settings and authoritative render path. A selected GPU backend never silently falls back to CPU: missing dispatchers return `backend_unavailable`, failed recording returns `dispatch_failed`, and the reference CPU path is invoked only when explicitly selected by the caller.

The stable C ABI accepts packed RGBA32F input/output buffers for Flutter FFI. The app owns crop handles and transform gestures; DigitorEngine owns sampling, geometry mapping and final pixels.
