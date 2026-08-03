#pragma once

#include "digitor/digitor.h"
#include "digitor/windows_vulkan_effect_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct WindowsVulkanBuiltinEffectShaderCode final {
  const std::uint32_t* words{};
  std::size_t word_count{};
};

struct WindowsVulkanBuiltinEffectShadersBindings final {
  void* physical_device{};
  void* device{};
  std::uint32_t queue_family_index{};
  WindowsVulkanBuiltinEffectShaderCode rgba8_compute_shader;
  WindowsVulkanBuiltinEffectShaderCode rgba16f_compute_shader;
};

struct WindowsVulkanBuiltinEffectShadersResult final {
  WindowsVulkanEffectDispatch dispatch;
  std::shared_ptr<void> lifetime;
  std::string package_identity;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] WindowsVulkanBuiltinEffectShadersResult
create_windows_vulkan_builtin_effect_shaders(
    WindowsVulkanBuiltinEffectShadersBindings bindings) noexcept;

}  // namespace digitor
