#include "cpu/cpu_backend.hpp"

#include <cstring>

namespace digitor {

bool CpuBackend::initialize(bool enable_validation) {
    (void)enable_validation;
    initialized_ = true;
    return true;
}

void CpuBackend::shutdown() noexcept {
    initialized_ = false;
}

DigitorRendererInfo CpuBackend::info() const noexcept {
    DigitorRendererInfo result{};
    result.backend = DIGITOR_RENDERER_CPU;
    std::strncpy(result.backend_name, "CPU Reference Renderer", sizeof(result.backend_name) - 1);
    std::strncpy(result.device_name, "Host CPU", sizeof(result.device_name) - 1);
    result.is_gpu = 0;
    result.supports_compute = 1;
    result.supports_fp16 = 0;
    result.supports_fp32 = 1;
    return result;
}

}  // namespace digitor
