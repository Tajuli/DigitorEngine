#include "cpu/cpu_backend.hpp"

#include "core/string_utils.hpp"

namespace digitor {

bool CpuBackend::initialize(bool enable_validation) {
  (void)enable_validation;
  initialized_ = true;
  return true;
}

void CpuBackend::shutdown() noexcept { initialized_ = false; }

DigitorRendererInfo CpuBackend::info() const noexcept {
  DigitorRendererInfo result{};
  result.backend = DIGITOR_RENDERER_CPU;
  copy_bounded(result.backend_name, "CPU Reference Renderer");
  copy_bounded(result.device_name, "Host CPU");
  result.is_gpu = 0;
  result.supports_compute = 1;
  result.supports_fp16 = 0;
  result.supports_fp32 = 1;
  return result;
}

DigitorResult
CpuBackend::render_rgba8(uint32_t width, uint32_t height,
                         std::span<const uint8_t> source,
                         std::vector<uint8_t> &destination) noexcept {
  const auto size = static_cast<std::size_t>(width) * height * 4;
  if (!width || !height || (!source.empty() && source.size() != size))
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    destination.assign(size, 0);
    if (!source.empty())
      std::copy(source.begin(), source.end(), destination.begin());
    else
      for (std::size_t i = 3; i < size; i += 4)
        destination[i] = 255;
  } catch (...) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult CpuBackend::grade_rgba32f(std::span<const Color> source,
                                        std::span<Color> destination,
                                        const ColorGrade &parameters) noexcept {
  if (source.size() != destination.size())
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  begin_grade_provenance(DIGITOR_RENDERER_CPU, false, "Host CPU",
                         "C++20 host compiler", "CPU grade_color",
                         "CPU direct loop");
  grade_image_cpu(source.data(), destination.data(), source.size(), parameters);
  provenance_.output_written = true;
  provenance_.cpu_color_reference_invocations =
      cpu_color_reference_count() - provenance_.cpu_color_reference_invocations;
  return DIGITOR_RESULT_OK;
}

} // namespace digitor
