#include "gpu/gpu_backend.hpp"
namespace digitor {
#ifdef DIGITOR_HAS_VULKAN
std::unique_ptr<IRenderBackend> create_vulkan_backend();
#endif
std::unique_ptr<IRenderBackend> create_native_backend(DigitorRendererBackend backend) {
#ifdef DIGITOR_HAS_VULKAN
    if (backend == DIGITOR_RENDERER_VULKAN) return create_vulkan_backend();
#else
    (void)backend;
#endif
    return nullptr;
}
}
