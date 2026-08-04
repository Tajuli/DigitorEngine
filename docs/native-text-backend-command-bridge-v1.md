# Native Text Backend Command Bridge v1

This milestone connects the engine-owned text packet/runtime contract to backend-specific command submission without moving text behavior into Flutter.

Supported backend identities are Vulkan, Direct3D 12, Metal and OpenGL ES. The bridge validates native handles, atlas generations, GPU-ready packets and target dimensions; records atlas upload, resource transition, pipeline/buffer binding, viewport and indexed-draw commands; and submits one authoritative command list through a backend callback.

Preview and export use the same packet digest and command order. Atlas uploads are reused per backend generation. Missing native handles, stale packets and backend rejection fail explicitly; there is no silent CPU fallback after GPU submission is requested.

This is a native command bridge and qualification contract. Platform SDK adapters still need to translate the recorded commands into VkCommandBuffer, ID3D12GraphicsCommandList, MTLRenderCommandEncoder and GLES calls in their existing backend modules.
