# Production Time Remap v1

This feature builds one deterministic timeline-to-source sampling plan for preview and export. It supports Hermite speed ramps, reverse motion through descending source-time keyframes, freeze frames, nearest/blended/optical-flow sampling identities, source-bound clamping, per-frame speed metadata, stable digests, native backend dispatch validation, and a stable C ABI for Flutter.

GPU requests never silently fall back to CPU. Vulkan, Direct3D 12, Metal, and OpenGL ES backends consume the authoritative sample plan and dispatch packet. Optical-flow synthesis itself remains the responsibility of the engine's motion-estimation/interpolation backend; this feature selects and schedules it consistently.
