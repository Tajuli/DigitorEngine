#include "gpu/gpu_backend.hpp"

#include "platform/platform.hpp"

namespace digitor {

std::unique_ptr<IRenderBackend> create_gpu_backend(
    DigitorRendererBackend preferred
) {
    // Foundation milestone:
    // GPU backends are declared in the architecture but not yet implemented.
    // AUTO intentionally returns nullptr so Engine can select CPU fallback.
    // Future milestones will add Vulkan, Metal, D3D12, and OpenGL ES.
    (void)preferred;
    return nullptr;
}

}  // namespace digitor
