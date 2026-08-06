#include "digitor/digitor.h"

#include <cstring>
#include <iostream>
#include <string_view>

namespace {

int fail(const char* message) {
  std::cerr << "release qualification failed: " << message << '\n';
  return 1;
}

}  // namespace

int main() {
  const char* version = digitor_get_version();
  if (version == nullptr || std::strlen(version) == 0) {
    return fail("runtime version is missing");
  }
  if (std::string_view(version) != "5.51.0") {
    std::cerr << "expected version 5.51.0, got " << version << '\n';
    return 1;
  }

  static_assert(DIGITOR_RESULT_OK == 0);
  static_assert(DIGITOR_RENDERER_CPU == 100);
  static_assert(DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION == 1u);
  static_assert(DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION == 1u);

  DigitorEngineConfig config{};
  config.preferred_backend = DIGITOR_RENDERER_CPU;
  config.enable_validation = 1;
  config.allow_cpu_fallback = 1;

  const DigitorResult initialized = digitor_initialize(&config);
  if (initialized != DIGITOR_RESULT_OK) {
    return fail("explicit CPU initialization failed");
  }

  DigitorRendererInfo info{};
  if (digitor_get_renderer_info(&info) != DIGITOR_RESULT_OK) {
    digitor_shutdown();
    return fail("renderer info query failed");
  }
  if (info.backend != DIGITOR_RENDERER_CPU || info.is_gpu != 0) {
    digitor_shutdown();
    return fail("explicit CPU selection returned the wrong backend");
  }

  DigitorRenderContext* context = nullptr;
  if (digitor_create_render_context(&context) != DIGITOR_RESULT_OK || context == nullptr) {
    digitor_shutdown();
    return fail("render context creation failed");
  }

  DigitorTextureDesc texture_desc{};
  texture_desc.width = 16;
  texture_desc.height = 16;
  texture_desc.format = DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  texture_desc.usage = DIGITOR_TEXTURE_USAGE_SAMPLED |
                       DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION;

  DigitorTexture* texture = nullptr;
  if (digitor_create_texture(context, &texture_desc, &texture) != DIGITOR_RESULT_OK ||
      texture == nullptr) {
    digitor_destroy_render_context(context);
    digitor_shutdown();
    return fail("texture creation failed");
  }

  if (digitor_destroy_texture(texture) != DIGITOR_RESULT_OK) {
    digitor_destroy_render_context(context);
    digitor_shutdown();
    return fail("texture destruction failed");
  }
  if (digitor_destroy_render_context(context) != DIGITOR_RESULT_OK) {
    digitor_shutdown();
    return fail("render context destruction failed");
  }
  if (digitor_shutdown() != DIGITOR_RESULT_OK) {
    return fail("engine shutdown failed");
  }

  std::cout << "DigitorEngine " << version
            << " release qualification smoke test passed\n";
  return 0;
}
