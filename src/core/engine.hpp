#pragma once

#include <memory>
#include <mutex>

#include "digitor/digitor.h"
#include "core/render_context.hpp"
#include "gpu/gpu_backend.hpp"

namespace digitor {

class Engine final {
public:
    static Engine& instance();

    DigitorResult initialize(const DigitorEngineConfig& config);
    DigitorResult shutdown();

    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] DigitorRendererInfo renderer_info() const noexcept;

    DigitorResult create_context(RenderContext** out_context);
    DigitorResult destroy_context(RenderContext* context);

private:
    Engine() = default;

    mutable std::mutex mutex_;
    bool initialized_{false};
    DigitorEngineConfig config_{};
    std::unique_ptr<IRenderBackend> backend_;
};

}  // namespace digitor
