#include "cpu/cpu_backend.hpp"

#include "core/string_utils.hpp"
#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace digitor {
namespace {
constexpr std::size_t kByteGrain = 256U * 1024U;
constexpr std::size_t kPixelGrain = 64U * 1024U;
}

CpuBackend::~CpuBackend() = default;

bool CpuBackend::initialize(bool enable_validation) {
  (void)enable_validation;
  try {
    executor_ = std::make_unique<CpuParallelExecutor>();
    initialized_ = true;
    return true;
  } catch (...) {
    executor_.reset();
    initialized_ = false;
    return false;
  }
}

void CpuBackend::shutdown() noexcept {
  executor_.reset();
  initialized_ = false;
}

DigitorRendererInfo CpuBackend::info() const noexcept {
  DigitorRendererInfo result{};
  result.backend = DIGITOR_RENDERER_CPU;
  copy_bounded(result.backend_name, "CPU Parallel Renderer");
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
  if (!initialized_ || !executor_) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (!width || !height ||
      width > std::numeric_limits<std::size_t>::max() / height / 4u)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto size = static_cast<std::size_t>(width) * height * 4u;
  if (!source.empty() && source.size() != size) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    destination.resize(size);
    if (!source.empty()) {
      executor_->parallel_for(size, kByteGrain,
                              [&](std::size_t begin, std::size_t end) {
        std::copy(source.begin() + static_cast<std::ptrdiff_t>(begin),
                  source.begin() + static_cast<std::ptrdiff_t>(end),
                  destination.begin() + static_cast<std::ptrdiff_t>(begin));
      });
    } else {
      executor_->parallel_for(static_cast<std::size_t>(width) * height,
                              kPixelGrain,
                              [&](std::size_t begin, std::size_t end) {
        for (std::size_t pixel = begin; pixel < end; ++pixel) {
          const auto offset = pixel * 4U;
          destination[offset + 0U] = 0U;
          destination[offset + 1U] = 0U;
          destination[offset + 2U] = 0U;
          destination[offset + 3U] = 255U;
        }
      });
    }
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult CpuBackend::grade_rgba32f(std::span<const Color> source,
                                        std::span<Color> destination,
                                        const ColorGrade &parameters) noexcept {
  if (!initialized_ || !executor_) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (source.size() != destination.size())
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    begin_grade_provenance(DIGITOR_RENDERER_CPU, false, "Host CPU",
                           "C++20 host compiler", "CPU grade_color",
                           "CPU deterministic parallel ranges");
    const auto before = cpu_color_reference_count();
    executor_->parallel_for(source.size(), kPixelGrain,
                            [&](std::size_t begin, std::size_t end) {
      grade_image_cpu(source.data() + begin, destination.data() + begin,
                      end - begin, parameters);
    });
    provenance_.output_written = true;
    provenance_.cpu_color_reference_invocations =
        cpu_color_reference_count() - before;
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult CpuBackend::execute_curves_rgba32f(
    std::span<const Color> source, std::span<Color> destination,
    const CompiledRgbCurves& curves) noexcept {
  if (!initialized_ || !executor_) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (source.size() != destination.size()) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    const auto before = cpu_curve_reference_count();
    executor_->parallel_for(source.size(), kPixelGrain,
                            [&](std::size_t begin, std::size_t end) {
      curves.apply(source.subspan(begin, end - begin),
                   destination.subspan(begin, end - begin));
    });
    provenance_.cpu_curve_invocations = cpu_curve_reference_count() - before;
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

} // namespace digitor
