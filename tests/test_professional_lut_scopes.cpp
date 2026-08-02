#include "digitor/professional_lut_scopes.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace digitor;

int main() {
  std::string diagnostic;
  const std::string cube =
      "LUT_3D_SIZE 2\n"
      "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
      "0 0 1\n1 0 1\n0 1 1\n1 1 1\n";

  ProfessionalLutEngine cpu;
  assert(cpu.load_text("identity", ProfessionalLutFormat::cube, cube,
                       &diagnostic) == DIGITOR_RESULT_OK);
  assert(cpu.inventory().size() == 1u);
  std::vector<Color> source{{0.25f, 0.5f, 0.75f, 0.4f}};
  std::vector<Color> output(1);
  assert(cpu.apply("identity", source, output, LutInterpolation::tetrahedral,
                   false, LutGpuBackend::vulkan, LutPrecision::fp32,
                   &diagnostic) == DIGITOR_RESULT_OK);
  assert(output[0].a == source[0].a);
  assert(cpu.apply("identity", source, output, LutInterpolation::tetrahedral,
                   true, LutGpuBackend::vulkan, LutPrecision::fp16,
                   &diagnostic) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  std::uint64_t uploads{};
  ProfessionalLutCallbacks callbacks;
  callbacks.upload_gpu = [&](const ProfessionalLut& lut, LutInterpolation,
                             LutPrecision, LutGpuResource& resource,
                             std::string& error) {
    ++uploads;
    resource.native_handle = 42;
    resource.edge_size = lut.metadata.edge_size;
    error.clear();
    return DIGITOR_RESULT_OK;
  };
  callbacks.apply_gpu = [](const LutGpuResource&, std::span<const Color> input,
                           std::span<Color> destination, std::string& error) {
    std::copy(input.begin(), input.end(), destination.begin());
    error.clear();
    return DIGITOR_RESULT_OK;
  };
  ProfessionalLutEngine gpu(std::move(callbacks), 2);
  assert(gpu.load_text("identity", ProfessionalLutFormat::cube, cube,
                       &diagnostic) == DIGITOR_RESULT_OK);
  assert(gpu.apply("identity", source, output, LutInterpolation::tetrahedral,
                   true, LutGpuBackend::metal, LutPrecision::fp16,
                   &diagnostic) == DIGITOR_RESULT_OK);
  assert(gpu.apply("identity", source, output, LutInterpolation::tetrahedral,
                   true, LutGpuBackend::metal, LutPrecision::fp16,
                   &diagnostic) == DIGITOR_RESULT_OK);
  assert(uploads == 1u);
  assert(gpu.telemetry().cache_hits == 1u);

  ColorManagementConfig color_config;
  ProfessionalColorManagement color(color_config);
  ScopeConfig scope_config;
  ScopeResult result;
  std::vector<Color> frame(4, Color{0.5f, 0.25f, 0.75f, 1.0f});
  ProductionGpuScopes fallback({}, true);
  assert(fallback.generate(color, frame, 2, 2, scope_config, result, false,
                           &diagnostic) == DIGITOR_RESULT_OK);
  assert(fallback.telemetry().scope_fallbacks == 1u);

  GpuScopeCallbacks scope_callbacks;
  scope_callbacks.dispatch = [](std::span<const Color> pixels,
                                std::uint32_t width, std::uint32_t height,
                                const ScopeConfig&, ScopeResult& out,
                                std::string& error) {
    out = {};
    out.source_width = width;
    out.source_height = height;
    out.sampled_pixels = pixels.size();
    error.clear();
    return DIGITOR_RESULT_OK;
  };
  ProductionGpuScopes scopes(std::move(scope_callbacks), false);
  assert(scopes.generate(color, frame, 2, 2, scope_config, result, true,
                         &diagnostic) == DIGITOR_RESULT_OK);
  assert(result.sampled_pixels == 4u);
  assert(scopes.telemetry().scope_dispatches == 1u);
  return 0;
}
