#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

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
    DigitorResult render_preview_rgba8(uint32_t, uint32_t, std::span<const uint8_t>,
        std::vector<uint8_t>&);

private:
    Engine() = default;

    mutable std::mutex mutex_;
    bool initialized_{false};
    DigitorEngineConfig config_{};
    std::unique_ptr<IRenderBackend> backend_;
    std::unordered_set<RenderContext*> contexts_;
};

}  // namespace digitor
