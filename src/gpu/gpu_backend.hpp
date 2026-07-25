#pragma once

#include <memory>
#include <functional>

#include "digitor/digitor.h"
#include "platform/platform.hpp"

namespace digitor {

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual bool initialize(bool enable_validation) = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual DigitorRendererInfo info() const noexcept = 0;
};

std::unique_ptr<IRenderBackend> create_gpu_backend(
    DigitorRendererBackend preferred
);

using BackendFactory = std::function<std::unique_ptr<IRenderBackend>(DigitorRendererBackend)>;

// Internal selection seam used to test platform policy without requiring GPU hardware.
std::unique_ptr<IRenderBackend> select_gpu_backend(
    HostPlatform platform,
    DigitorRendererBackend preferred,
    const BackendFactory& factory);

}  // namespace digitor
