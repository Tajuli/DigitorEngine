from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match in {path}, got {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/gpu/preview_consumer.hpp",
    "  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};\n"
    "  GpuPrecisionMode precision{GpuPrecisionMode::Float32};\n"
    "};",
    "  // Destination backing format. Source frames remain exact-format by default.\n"
    "  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};\n"
    "  GpuPrecisionMode precision{GpuPrecisionMode::Float32};\n"
    "  // Opt-in for GPU consumers that own a floating-point RGBA destination and\n"
    "  // explicitly support hardware conversion between RGBA16F and RGBA32F.\n"
    "  bool allow_float_rgba_precision_transition{};\n"
    "};",
)

replace_once(
    "src/gpu/preview_consumer.cpp",
    "  const auto& source = frame->metadata();\n"
    "  if (frame->backend() != metadata_.backend || source.width != metadata_.width ||\n"
    "      source.height != metadata_.height || source.format != metadata_.format ||\n"
    "      metadata_.precision != GpuPrecisionMode::Float32 || !metadata_.context_identity)\n"
    "    return DIGITOR_RESULT_INVALID_ARGUMENT;",
    "  const auto& source = frame->metadata();\n"
    "  const auto is_float_rgba = [](DigitorPixelFormat format) noexcept {\n"
    "    return format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||\n"
    "           format == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;\n"
    "  };\n"
    "  const bool compatible_format = source.format == metadata_.format ||\n"
    "      (metadata_.allow_float_rgba_precision_transition &&\n"
    "       is_float_rgba(source.format) && is_float_rgba(metadata_.format));\n"
    "  if (frame->backend() != metadata_.backend || source.width != metadata_.width ||\n"
    "      source.height != metadata_.height || !compatible_format ||\n"
    "      metadata_.precision != GpuPrecisionMode::Float32 || !metadata_.context_identity)\n"
    "    return DIGITOR_RESULT_INVALID_ARGUMENT;",
)

replace_once(
    "src/gpu/gles_backend.cpp",
    "PreviewConsumerMetadata{DIGITOR_RENDERER_OPENGL_ES,this,m.width,m.height,m.format,GpuPrecisionMode::Float32}",
    "PreviewConsumerMetadata{DIGITOR_RENDERER_OPENGL_ES,this,m.width,m.height,\n"
    "      DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuPrecisionMode::Float32,true}",
)

Path("tests/test_preview_consumer_format_transition.cpp").write_text(
    '''#include "gpu/preview_consumer.hpp"

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
'''
)

cmake = Path("cmake/DigitorEngineBase.cmake")
cmake_text = cmake.read_text()
anchor = "    add_test(NAME digitor_native_gpu_tests COMMAND digitor_native_gpu_tests)\n"
block = '''    add_executable(digitor_preview_consumer_format_transition_test
        tests/test_preview_consumer_format_transition.cpp)
    set_target_properties(digitor_preview_consumer_format_transition_test PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_preview_consumer_format_transition_test PRIVATE Digitor::Engine)
    target_include_directories(digitor_preview_consumer_format_transition_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_preview_consumer_format_transition_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_preview_consumer_format_transition_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_preview_consumer_format_transition
        COMMAND digitor_preview_consumer_format_transition_test)

'''
if cmake_text.count(anchor) != 1:
    raise SystemExit("native GPU test anchor missing or ambiguous")
cmake.write_text(cmake_text.replace(anchor, anchor + block, 1))

Path(".github/workflows/agent-patch-gles-preview-format-transition.yml").unlink()
Path("tools/agent_patch_gles_preview_format_transition.py").unlink()
