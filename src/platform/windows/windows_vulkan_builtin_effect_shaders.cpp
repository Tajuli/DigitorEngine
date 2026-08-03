#include "digitor/windows_vulkan_builtin_effect_shaders.hpp"

#if defined(_WIN32)

// The Vulkan shader package is platform-neutral. Reuse the already-qualified
// Android implementation as the single source of truth instead of copying the
// descriptor/pipeline/dispatch code and allowing platform drift.
#ifndef __ANDROID__
#define __ANDROID__ 1
#define DIGITOR_UNDEFINE_ANDROID_AFTER_SHARED_VULKAN_PACKAGE 1
#endif
#include "../android/android_vulkan_builtin_effect_shaders.cpp"
#if defined(DIGITOR_UNDEFINE_ANDROID_AFTER_SHARED_VULKAN_PACKAGE)
#undef DIGITOR_UNDEFINE_ANDROID_AFTER_SHARED_VULKAN_PACKAGE
#undef __ANDROID__
#endif

namespace digitor {

WindowsVulkanBuiltinEffectShadersResult
create_windows_vulkan_builtin_effect_shaders(
    WindowsVulkanBuiltinEffectShadersBindings bindings) noexcept {
  AndroidVulkanBuiltinEffectShadersBindings shared{};
  shared.physical_device = bindings.physical_device;
  shared.device = bindings.device;
  shared.queue_family_index = bindings.queue_family_index;
  shared.rgba8_compute_shader = {
      bindings.rgba8_compute_shader.words,
      bindings.rgba8_compute_shader.word_count};
  shared.rgba16f_compute_shader = {
      bindings.rgba16f_compute_shader.words,
      bindings.rgba16f_compute_shader.word_count};

  auto package = create_android_vulkan_builtin_effect_shaders(shared);
  WindowsVulkanBuiltinEffectShadersResult out{};
  out.dispatch = std::move(package.dispatch);
  out.lifetime = std::move(package.lifetime);
  out.result = package.result;
  out.diagnostic = std::move(package.diagnostic);
  if (package) {
    out.package_identity =
        "digitor.windows.vulkan.builtin-effects.pipeline.v1";
  }
  return out;
}

}  // namespace digitor

#else

namespace digitor {
WindowsVulkanBuiltinEffectShadersResult
create_windows_vulkan_builtin_effect_shaders(
    WindowsVulkanBuiltinEffectShadersBindings) noexcept {
  WindowsVulkanBuiltinEffectShadersResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic =
      "Windows Vulkan built-in effect pipelines are only available on Windows";
  return out;
}
}  // namespace digitor

#endif
