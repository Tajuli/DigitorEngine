# Production Motion Blur v1

This feature provides deterministic vector-driven motion blur for RGBA32F frames. The CPU reference path uses confidence-weighted shutter sampling with bilinear reconstruction, while GPU callers use validated Vulkan, Direct3D 12, Metal, or OpenGL ES dispatch packets. Preview and export share the same settings and frame digest contract. GPU execution never silently falls back to CPU. The C ABI accepts packed RGBA and XY-confidence float buffers for Flutter integration.
