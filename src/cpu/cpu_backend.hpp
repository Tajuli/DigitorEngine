#pragma once

#include "gpu/gpu_backend.hpp"

namespace digitor {

class CpuBackend final : public IRenderBackend {
public:
  bool initialize(bool enable_validation) override;
  void shutdown() noexcept override;
  [[nodiscard]] DigitorRendererInfo info() const noexcept override;
  DigitorResult render_rgba8(uint32_t, uint32_t, std::span<const uint8_t>,
                             std::vector<uint8_t> &) noexcept override;
  DigitorResult grade_rgba32f(std::span<const Color>, std::span<Color>,
                              const ColorGrade &) noexcept override;
  DigitorResult curves_rgba32f(std::span<const Color>, std::span<Color>,
                               const CompiledRgbCurves&) noexcept override;

private:
  bool initialized_{false};
};

} // namespace digitor
