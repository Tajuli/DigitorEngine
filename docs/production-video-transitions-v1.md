# Production Video Transitions v1

DigitorEngine owns deterministic transition rendering for cross dissolve, dip-to-color, directional wipe, and directional slide transitions.

Preview and export call the same RGBA32F processing path and produce the same frame digest for identical inputs and settings. GPU callers submit validated Vulkan, Direct3D 12, Metal, or OpenGL ES dispatch packets; a missing GPU dispatcher returns an explicit backend-unavailable status and never silently selects CPU execution.

The stable C ABI accepts packed RGBA32F frames for Flutter FFI integration. Timeline/UI code owns transition placement and controls, while pixel generation remains authoritative inside DigitorEngine.
