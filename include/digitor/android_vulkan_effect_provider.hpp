#pragma once

#include "digitor/digitor.h"
#include "digitor/native_effects.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

// The provider owns Vulkan command-buffer allocation, transient images,
// barriers, queue submission and completion. The shader package callback only
// binds its pipeline/descriptors and records one dispatch into command_buffer.
using AndroidVulkanEffectDispatch = std::function<bool(
    void* command_buffer,
    const NativeEffectPass& pass,
    std::uint64_t input_image,
    std::uint64_t output_image,
    std::string& diagnostic)>;

struct AndroidVulkanEffectProviderBindings final {
  // VkPhysicalDevice, VkDevice, VkQueue and VkCommandPool bridged as void*.
  void* physical_device{};
  void* device{};
  void* queue{};
  void* command_pool{};
  std::uint32_t queue_family_index{};
  std::uint64_t device_identity{};
  std::string shader_package_identity;
  AndroidVulkanEffectDispatch dispatch;
  bool supports_hdr{true};
  bool supports_external_memory{true};
  bool supports_external_synchronization{true};
};

struct AndroidVulkanEffectProviderResult final {
  NativeEffectBackendProvider provider;
  std::shared_ptr<void> lifetime;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

// External input/output image handles must be VkImage values owned by the same
// VkDevice, already synchronized by the caller and usable in GENERAL layout.
[[nodiscard]] AndroidVulkanEffectProviderResult
create_android_vulkan_effect_provider(
    AndroidVulkanEffectProviderBindings bindings) noexcept;

}  // namespace digitor
