#pragma once

#include "digitor/digitor.h"
#include "digitor/native_effects.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

using WindowsVulkanEffectDispatch = std::function<bool(
    void* command_buffer,
    const NativeEffectPass& pass,
    std::uint64_t input_image,
    std::uint64_t output_image,
    std::string& diagnostic)>;

struct WindowsVulkanEffectProviderBindings final {
  void* physical_device{};
  void* device{};
  void* queue{};
  void* command_pool{};
  std::uint32_t queue_family_index{};
  std::uint64_t device_identity{};
  std::string shader_package_identity;
  WindowsVulkanEffectDispatch dispatch;
  bool supports_hdr{true};
  bool supports_external_memory{true};
  bool supports_external_synchronization{true};
};

struct WindowsVulkanEffectProviderResult final {
  NativeEffectBackendProvider provider;
  std::shared_ptr<void> lifetime;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] WindowsVulkanEffectProviderResult
create_windows_vulkan_effect_provider(
    WindowsVulkanEffectProviderBindings bindings) noexcept;

}  // namespace digitor
