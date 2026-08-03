#include "digitor/windows_vulkan_builtin_effect_shaders.hpp"
#include "digitor/digitor_windows_builtin_effect_rgba8_spv.hpp"
#include "digitor/digitor_windows_builtin_effect_rgba16f_spv.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char* message) {
  std::cerr << "QUALIFICATION_FAILED=" << message << '\n';
  return 1;
}

}  // namespace

int main() {
  using namespace digitor;
  using namespace digitor::generated;

  if (digitor_windows_builtin_effect_rgba8_spv_word_count < 5 ||
      digitor_windows_builtin_effect_rgba16f_spv_word_count < 5) {
    return fail("SPIR-V payload is too small");
  }
  if (digitor_windows_builtin_effect_rgba8_spv[0] != 0x07230203u ||
      digitor_windows_builtin_effect_rgba16f_spv[0] != 0x07230203u) {
    return fail("SPIR-V magic mismatch");
  }

  WindowsVulkanBuiltinEffectShadersBindings invalid{};
  invalid.rgba8_compute_shader = {
      digitor_windows_builtin_effect_rgba8_spv,
      digitor_windows_builtin_effect_rgba8_spv_word_count};
  invalid.rgba16f_compute_shader = {
      digitor_windows_builtin_effect_rgba16f_spv,
      digitor_windows_builtin_effect_rgba16f_spv_word_count};
  const auto result = create_windows_vulkan_builtin_effect_shaders(invalid);
  if (result.result != DIGITOR_RESULT_INVALID_ARGUMENT) {
    return fail("invalid device bindings did not fail closed");
  }
  if (result.diagnostic.empty()) {
    return fail("failure diagnostic is empty");
  }

  std::cout << "QUALIFICATION=PASS\n";
  std::cout << "PACKAGE=digitor.windows.vulkan.builtin-effects.pipeline.v1\n";
  std::cout << "EFFECT_COUNT=9\n";
  std::cout << "SDR_SPIRV_WORDS="
            << digitor_windows_builtin_effect_rgba8_spv_word_count << '\n';
  std::cout << "HDR_SPIRV_WORDS="
            << digitor_windows_builtin_effect_rgba16f_spv_word_count << '\n';
  std::cout << "RUNTIME_EXECUTION=PHYSICAL_WINDOWS_VULKAN_REQUIRED\n";
  return 0;
}
