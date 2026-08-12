#include "digitor/native_node_executor.hpp"
#include "gpu/gpu_backend.hpp"

#include <atomic>
#include <cassert>
#include <memory>

using namespace digitor;

namespace {
class FakeGpuBackend final : public IRenderBackend {
public:
  bool initialize(bool) override { return true; }
  void shutdown() noexcept override {}
  DigitorRendererInfo info() const noexcept override {
    DigitorRendererInfo value{};
    value.backend = DIGITOR_RENDERER_OPENGL_ES;
    value.is_gpu = 1;
    value.supports_compute = 1;
    value.supports_fp16 = 1;
    value.supports_fp32 = 1;
    return value;
  }

  int cpu_upload_calls{};
  int gpu_primary_calls{};

protected:
  DigitorResult execute_process_primary_wheels_gpu(
      std::span<const Color>, std::uint32_t, std::uint32_t, std::int64_t,
      const PrimaryWheelsParameters&, ProcessedGpuFramePtr& out) noexcept override {
    ++cpu_upload_calls;
    out.reset();
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }

  DigitorResult execute_process_primary_wheels_gpu(
      const GpuSourceResource& source, std::int64_t timestamp,
      const PrimaryWheelsParameters&, ProcessedGpuFramePtr& out) noexcept override {
    ++gpu_primary_calls;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_OPENGL_ES,
        GpuFrameMetadata{source.width, source.height,
                         DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp,
                         source.color_metadata_identity},
        9000 + static_cast<std::uint64_t>(gpu_primary_calls),
        std::make_shared<int>(2),
        std::make_shared<std::atomic_bool>(true), false);
    return DIGITOR_RESULT_OK;
  }
};

ProcessedGpuFramePtr make_frame(DigitorPixelFormat format) {
  static int context{};
  return std::make_shared<ProcessedGpuFrame>(
      &context, DIGITOR_RENDERER_OPENGL_ES,
      GpuFrameMetadata{4, 2, format, GpuFrameAlpha::straight, 41,
                       "linear-rgba"},
      7001, std::make_shared<int>(1),
      std::make_shared<std::atomic_bool>(true), false);
}
} // namespace

int main() {
  FakeGpuBackend backend;

  const auto rgba16 = make_frame(DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT);
  const auto source16 = backend.gpu_source(rgba16);
  assert(source16.context_identity != 0);
  assert(source16.format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT);
  assert(source16.usable_by(DIGITOR_RENDERER_OPENGL_ES,
                            source16.context_identity));

  const auto rgba32 = make_frame(DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT);
  const auto source32 = backend.gpu_source(rgba32);
  assert(source32.usable_by(DIGITOR_RENDERER_OPENGL_ES,
                            source32.context_identity));

  const auto rgba8 = make_frame(DIGITOR_PIXEL_FORMAT_RGBA8_UNORM);
  const auto source8 = backend.gpu_source(rgba8);
  assert(!source8.usable_by(DIGITOR_RENDERER_OPENGL_ES,
                            source8.context_identity));

  ProductionNodeGraph graph;
  const auto grade = graph.add_serial_after(graph.input_node(), "Imported grade");
  graph.select_node(grade);
  graph.add_operation_to_selected(
      make_primary_wheels_operation(PrimaryWheelsParameters::create()));

  const auto result = execute_native_node_graph(backend, graph, rgba16, 42);
  assert(result.status == NativeNodeGraphStatus::ok);
  assert(result.backend_result == DIGITOR_RESULT_OK);
  assert(result.frame);
  assert(result.frame->metadata().format == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT);
  assert(result.frame->metadata().width == 4 && result.frame->metadata().height == 2);
  assert(backend.gpu_primary_calls == 1);
  assert(backend.cpu_upload_calls == 0);

  // A no-op graph preserves the already-imported GPU frame directly. No CPU
  // upload or synthetic color pass is introduced by graph execution itself.
  ProductionNodeGraph passthrough;
  const auto direct = execute_native_node_graph(backend, passthrough, rgba16, 43);
  assert(direct.status == NativeNodeGraphStatus::ok);
  assert(direct.backend_result == DIGITOR_RESULT_OK);
  assert(direct.frame == rgba16);
  assert(direct.frame->metadata().format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT);
  assert(backend.gpu_primary_calls == 1);
  assert(backend.cpu_upload_calls == 0);

  // Non-floating sources remain outside backend-native floating-point passes.
  const auto invalid = execute_native_node_graph(backend, graph, rgba8, 44);
  assert(invalid.status == NativeNodeGraphStatus::backend_failure);
  assert(invalid.backend_result == DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(!invalid.frame);
  assert(backend.gpu_primary_calls == 1);
  assert(backend.cpu_upload_calls == 0);
}
