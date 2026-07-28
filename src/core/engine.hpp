#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include "core/render_context.hpp"
#include "digitor/digitor.h"
#include "gpu/gpu_backend.hpp"

namespace digitor {

class Engine final {
public:
  static Engine &instance();

  DigitorResult initialize(const DigitorEngineConfig &config);
  DigitorResult shutdown();

  [[nodiscard]] bool is_initialized() const noexcept;
  [[nodiscard]] DigitorRendererInfo renderer_info() const noexcept;

  DigitorResult create_context(RenderContext **out_context);
  DigitorResult destroy_context(RenderContext *context);
  DigitorResult render_preview_rgba8(uint32_t, uint32_t,
                                     std::span<const uint8_t>,
                                     std::vector<uint8_t> &);
  DigitorResult grade_rgba32f(std::span<const Color>, std::span<Color>,
                              const ColorGrade &);
  DigitorResult curves_rgba32f(std::span<const Color>, std::span<Color>,
                               const CompiledRgbCurves&);
  DigitorResult process_curves_gpu(std::span<const Color>, std::uint32_t,
      std::uint32_t, std::int64_t, const CompiledRgbCurves&, ProcessedGpuFramePtr&);
  DigitorResult present_gpu_frame(const ProcessedGpuFramePtr&);

private:
  Engine() = default;

  mutable std::mutex mutex_;
  bool initialized_{false};
  DigitorEngineConfig config_{};
  std::unique_ptr<IRenderBackend> backend_;
  std::unordered_set<RenderContext *> contexts_;
};

} // namespace digitor
