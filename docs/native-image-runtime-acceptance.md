# Acceptance gate

The single Native Image Runtime Qualification workflow checks Linux, Windows and macOS build compatibility, provider validation, GPU-lock/no-silent-fallback behavior, CPU selection, metadata/limit validation and app-facing open failure propagation. Physical device codec and GPU texture fixtures remain release qualification because hosted runners do not expose representative D3D12/Vulkan/Metal/OpenGL ES devices.
