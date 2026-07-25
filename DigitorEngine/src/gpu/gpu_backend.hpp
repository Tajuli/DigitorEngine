#pragma once

#include <memory>

#include "digitor/digitor.h"

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

}  // namespace digitor
