#include "digitor/ocio_color_pipeline.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace digitor;

int main() {
  OcioTransformRequest invalid;
  invalid.kind = OcioTransformKind::color_space;
  std::string diagnostic;

  AdvancedColorPipelineConfig native_config;
  native_config.enable_ocio = false;
  native_config.native_fallback.input = ManagedColorSpace::srgb;
  native_config.native_fallback.working = ManagedColorSpace::linear_rec709;
  native_config.native_fallback.display = ManagedColorSpace::srgb;
  native_config.native_fallback.output = ManagedColorSpace::srgb;
  native_config.native_fallback.tone_map = ToneMapMode::none;
  native_config.native_fallback.gamut_map = GamutMapMode::none;
  OcioColorPipeline native(native_config);
  assert(native.load(&diagnostic) == DIGITOR_RESULT_OK);
  assert(native.loaded());
  assert(native.inventory().valid);
  assert(native.validate_request(invalid, &diagnostic) == DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(!diagnostic.empty());

  OcioTransformRequest bypass;
  bypass.kind = OcioTransformKind::color_space;
  bypass.bypass = true;
  std::vector<Color> source{{0.2f, 0.4f, 0.8f, 0.25f},
                            {1.2f, -0.1f, 0.5f, 0.75f}};
  std::vector<Color> destination(source.size());
  assert(native.transform_image(bypass, source, destination, {}, &diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(destination[0].r == source[0].r);
  assert(destination[1].a == source[1].a);

  OcioTransformRequest native_request;
  native_request.kind = OcioTransformKind::color_space;
  native_request.source = "native-input";
  native_request.destination = "native-output";
  assert(native.transform_image(native_request, source, destination, {}, &diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(std::isfinite(destination[0].r));
  assert(destination[0].a == source[0].a);
  assert(native.telemetry().cpu_pixels == source.size());

  Color output{};
  const Color non_finite{NAN, 0.0f, 0.0f, 1.0f};
  assert(native.transform_pixel(native_request, non_finite, output, {}, &diagnostic) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  OcioGpuShader shader;
  assert(native.compile_gpu_shader(native_request, OcioGpuLanguage::glsl_vulkan,
                                   shader, {}, &diagnostic) ==
         DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  native.invalidate_processors();
  assert(native.telemetry().invalidations == 1);

  AdvancedColorPipelineConfig ocio_config;
  ocio_config.enable_ocio = true;
  ocio_config.require_ocio = true;
  ocio_config.config_text = R"OCIO(ocio_profile_version: 2
roles:
  scene_linear: lin
  default: lin
file_rules:
  - !<Rule> {name: Default, colorspace: lin}
colorspaces:
  - !<ColorSpace>
    name: lin
    family: test
    bitdepth: 32f
    isdata: false
    allocation: uniform
  - !<ColorSpace>
    name: display
    family: test
    bitdepth: 32f
    isdata: false
    allocation: uniform
    from_scene_reference: !<ExponentTransform> {value: [0.454545, 0.454545, 0.454545, 1]}
displays:
  Test:
    - !<View> {name: Default, colorspace: display}
active_displays: [Test]
active_views: [Default]
)OCIO";
  OcioColorPipeline ocio(ocio_config);
  const auto load_result = ocio.load(&diagnostic);
  if (OcioColorPipeline::compiled_with_ocio()) {
    assert(load_result == DIGITOR_RESULT_OK);
    assert(ocio.loaded());
    assert(ocio.inventory().valid);
    assert(ocio.inventory().color_spaces.size() == 2);
    assert(!ocio.telemetry().config_cache_id.empty());

    OcioTransformRequest request;
    request.kind = OcioTransformKind::color_space;
    request.source = "lin";
    request.destination = "display";
    assert(ocio.transform_image(request, source, destination, {}, &diagnostic) ==
           DIGITOR_RESULT_OK);
    const auto first = ocio.telemetry();
    assert(first.processor_compiles == 1);
    assert(ocio.transform_image(request, source, destination, {}, &diagnostic) ==
           DIGITOR_RESULT_OK);
    const auto second = ocio.telemetry();
    assert(second.processor_cache_hits >= 1);
    assert(destination[0].a == source[0].a);

    const auto gpu_result = ocio.compile_gpu_shader(
        request, OcioGpuLanguage::glsl_vulkan, shader, {}, &diagnostic);
    assert(gpu_result == DIGITOR_RESULT_OK ||
           gpu_result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    if (gpu_result == DIGITOR_RESULT_OK) {
      assert(!shader.source.empty());
      assert(!shader.cache_id.empty());
    }
  } else {
    assert(load_result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    assert(!diagnostic.empty());
  }

  std::cout << "OCIO 2.x and advanced color pipeline qualification passed\n";
  return 0;
}
