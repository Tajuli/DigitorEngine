#pragma once

#include "digitor/android_vulkan_effect_provider.hpp"
#include "digitor/digitor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct AndroidVulkanBuiltinEffectShaderCode final {
  const std::uint32_t* words{};
  std::size_t word_count{};
};

struct AndroidVulkanBuiltinEffectShadersBindings final {
  void* physical_device{};
  void* device{};
  std::uint32_t queue_family_index{};
  AndroidVulkanBuiltinEffectShaderCode rgba8_compute_shader;
  AndroidVulkanBuiltinEffectShaderCode rgba16f_compute_shader;
};

struct AndroidVulkanBuiltinEffectShadersResult final {
  AndroidVulkanEffectDispatch dispatch;
  std::shared_ptr<void> lifetime;
  std::string package_identity;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

// Creates the repository-owned Vulkan compute package for all built-in effects.
// Both SPIR-V variants must be generated from
// shaders/vulkan/digitor_builtin_effect.comp by the repository build rules.
[[nodiscard]] AndroidVulkanBuiltinEffectShadersResult
create_android_vulkan_builtin_effect_shaders(
    AndroidVulkanBuiltinEffectShadersBindings bindings) noexcept;

}  // namespace digitor
