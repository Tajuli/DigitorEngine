#include "gpu/preview_consumer.hpp"

#include <atomic>
#include <cassert>
#include <memory>

using namespace digitor;

namespace {
ProcessedGpuFramePtr frame(const void* context, DigitorPixelFormat format,
                           std::uint64_t identity) {
  return std::make_shared<ProcessedGpuFrame>(
      context, DIGITOR_RENDERER_OPENGL_ES,
      GpuFrameMetadata{4, 2, format, GpuFrameAlpha::straight, 10, "linear-rgba"},
      identity, std::make_shared<int>(1),
      std::make_shared<std::atomic_bool>(true), false);
}

std::shared_ptr<PreviewConsumerDestination> consumer(
    const void* context, bool allow_transition, int& native_submissions) {
  return std::make_shared<PreviewConsumerDestination>(
      PreviewConsumerMetadata{DIGITOR_RENDERER_OPENGL_ES, context, 4, 2,
                              DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                              GpuPrecisionMode::Float32, allow_transition},
      1, std::make_shared<int>(1), std::make_shared<std::atomic_bool>(true),
      [&native_submissions](const ProcessedGpuFramePtr&,
                            const std::shared_ptr<void>&) noexcept {
        ++native_submissions;
        return DIGITOR_RESULT_OK;
      });
}
} // namespace

int main() {
  int context{};
  int submissions{};
  auto transition = consumer(&context, true, submissions);
  assert(transition->metadata().format == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT);
  assert(transition->metadata().allow_float_rgba_precision_transition);

  // One persistent GLES destination must accept imported RGBA16F and native
  // node/color RGBA32F frames without changing its physical RGBA32F contract.
  assert(transition->submit(frame(&context, DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT, 1)) ==
         DIGITOR_RESULT_OK);
  assert(transition->submit(frame(&context, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT, 2)) ==
         DIGITOR_RESULT_OK);
  assert(transition->submission_count() == 2);
  assert(submissions == 2);

  // The opt-in is deliberately narrow: unrelated formats remain invalid.
  assert(transition->submit(frame(&context, DIGITOR_PIXEL_FORMAT_RGBA8_UNORM, 3)) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(transition->submission_count() == 2);
  assert(submissions == 2);

  // Other consumers retain the historical exact-format rule by default.
  int strict_submissions{};
  auto strict = consumer(&context, false, strict_submissions);
  assert(strict->submit(frame(&context, DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT, 4)) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(strict->submit(frame(&context, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT, 5)) ==
         DIGITOR_RESULT_OK);
  assert(strict->submission_count() == 1);
  assert(strict_submissions == 1);
}
