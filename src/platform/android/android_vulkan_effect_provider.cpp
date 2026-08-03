#include "digitor/android_vulkan_effect_provider.hpp"

#if defined(__ANDROID__)

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace digitor {
namespace {

struct OwnedImage final {
  VkDevice device{VK_NULL_HANDLE};
  VkImage image{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
};

VkFormat vk_effect_format(NativeEffectFormat format) noexcept {
  switch (format) {
    case NativeEffectFormat::rgba8_unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case NativeEffectFormat::bgra8_unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case NativeEffectFormat::rgba16_float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
  }
}

std::uint32_t find_memory_type(VkPhysicalDevice physical_device,
                               std::uint32_t bits,
                               VkMemoryPropertyFlags required) noexcept {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
  for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) != 0 &&
        (properties.memoryTypes[i].propertyFlags & required) == required) {
      return i;
    }
  }
  return UINT32_MAX;
}

VkImage resolve_image(const NativeEffectSurface& surface) noexcept {
  if (!surface.texture_handle) return VK_NULL_HANDLE;
  if (!surface.engine_owned) {
    return reinterpret_cast<VkImage>(surface.texture_handle);
  }
  const auto* owned = reinterpret_cast<const OwnedImage*>(surface.texture_handle);
  return owned ? owned->image : VK_NULL_HANDLE;
}

struct VulkanEffectState final {
  VkPhysicalDevice physical_device{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
  VkQueue queue{VK_NULL_HANDLE};
  VkCommandPool command_pool{VK_NULL_HANDLE};
  VkCommandBuffer command_buffer{VK_NULL_HANDLE};
  VkFence fence{VK_NULL_HANDLE};
  std::uint32_t queue_family_index{};
  std::uint64_t identity{};
  std::string shader_identity;
  AndroidVulkanEffectDispatch dispatch;
  std::mutex mutex;
  bool recording{};

  ~VulkanEffectState() {
    if (device != VK_NULL_HANDLE) {
      if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
      if (command_buffer != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
      }
    }
  }

  bool initialize(std::string& diagnostic) {
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = command_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocation, &command_buffer) != VK_SUCCESS) {
      diagnostic = "Android Vulkan effect command buffer allocation failed";
      return false;
    }
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
      diagnostic = "Android Vulkan effect fence creation failed";
      return false;
    }
    return true;
  }

  bool begin(std::string& diagnostic) {
    if (recording) return true;
    if (vkResetCommandBuffer(command_buffer, 0) != VK_SUCCESS) {
      diagnostic = "Android Vulkan effect command buffer reset failed";
      return false;
    }
    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
      diagnostic = "Android Vulkan effect command buffer begin failed";
      return false;
    }
    recording = true;
    return true;
  }

  void abort_recording() noexcept {
    if (recording) {
      vkEndCommandBuffer(command_buffer);
      vkResetCommandBuffer(command_buffer, 0);
    }
    recording = false;
  }

  bool submit_and_wait(std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!recording) {
      diagnostic = "Android Vulkan effect submission has no recorded passes";
      return false;
    }
    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      recording = false;
      diagnostic = "Android Vulkan effect command buffer end failed";
      return false;
    }
    recording = false;
    vkResetFences(device, 1, &fence);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) {
      diagnostic = "Android Vulkan effect queue submission failed";
      return false;
    }
    const VkResult wait = vkWaitForFences(device, 1, &fence, VK_TRUE, 30'000'000'000ull);
    if (wait != VK_SUCCESS) {
      diagnostic = wait == VK_TIMEOUT
          ? "Android Vulkan effect GPU completion timed out"
          : "Android Vulkan effect GPU completion failed";
      return false;
    }
    return true;
  }
};

}  // namespace

AndroidVulkanEffectProviderResult create_android_vulkan_effect_provider(
    AndroidVulkanEffectProviderBindings bindings) noexcept {
  AndroidVulkanEffectProviderResult out{};
  if (!bindings.physical_device || !bindings.device || !bindings.queue ||
      !bindings.command_pool || !bindings.device_identity ||
      bindings.shader_package_identity.empty() || !bindings.dispatch) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Android Vulkan effect provider bindings are incomplete";
    return out;
  }
  if (!bindings.supports_external_memory ||
      !bindings.supports_external_synchronization) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "Android Vulkan effects require external-memory and synchronization support";
    return out;
  }

  auto state = std::make_shared<VulkanEffectState>();
  state->physical_device = reinterpret_cast<VkPhysicalDevice>(bindings.physical_device);
  state->device = reinterpret_cast<VkDevice>(bindings.device);
  state->queue = reinterpret_cast<VkQueue>(bindings.queue);
  state->command_pool = reinterpret_cast<VkCommandPool>(bindings.command_pool);
  state->queue_family_index = bindings.queue_family_index;
  state->identity = bindings.device_identity;
  state->shader_identity = std::move(bindings.shader_package_identity);
  state->dispatch = std::move(bindings.dispatch);
  std::string diagnostic;
  if (!state->initialize(diagnostic)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = std::move(diagnostic);
    return out;
  }

  NativeEffectBackendProvider provider{};
  provider.backend = NativeEffectBackend::vulkan;
  provider.device_identity = state->identity;
  provider.supports_external_memory = true;
  provider.supports_external_synchronization = true;
  provider.supports_hdr = bindings.supports_hdr;
  provider.pass_count = [](const EffectDescriptor& descriptor,
                           const EffectInstance&, EffectQuality) {
    switch (descriptor.type) {
      case EffectType::blur:
      case EffectType::glow:
      case EffectType::motion_blur:
        return 2u;
      default:
        return 1u;
    }
  };
  provider.allocate_transient = [state](const NativeEffectSurface& prototype,
                                        NativeEffectSurface& output,
                                        std::string& diagnostic) {
    const VkFormat format = vk_effect_format(prototype.format);
    if (format == VK_FORMAT_UNDEFINED) {
      diagnostic = "unsupported Android Vulkan effect transient format";
      return false;
    }
    auto owned = std::make_unique<OwnedImage>();
    owned->device = state->device;
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {prototype.width, prototype.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(state->device, &image_info, nullptr, &owned->image) != VK_SUCCESS) {
      diagnostic = "Android Vulkan effect transient image creation failed";
      return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(state->device, owned->image, &requirements);
    const std::uint32_t type = find_memory_type(
        state->physical_device, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
      vkDestroyImage(state->device, owned->image, nullptr);
      diagnostic = "Android Vulkan effect device-local memory is unavailable";
      return false;
    }
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (vkAllocateMemory(state->device, &allocation, nullptr, &owned->memory) != VK_SUCCESS ||
        vkBindImageMemory(state->device, owned->image, owned->memory, 0) != VK_SUCCESS) {
      if (owned->memory) vkFreeMemory(state->device, owned->memory, nullptr);
      vkDestroyImage(state->device, owned->image, nullptr);
      diagnostic = "Android Vulkan effect transient memory allocation failed";
      return false;
    }
    output = prototype;
    output.texture_handle = reinterpret_cast<std::uint64_t>(owned.release());
    output.device_identity = state->identity;
    output.engine_owned = true;
    output.external_memory = false;
    output.cpu_mappable = false;
    return true;
  };
  provider.release_transient = [](const NativeEffectSurface& surface) {
    if (!surface.engine_owned || !surface.texture_handle) return;
    std::unique_ptr<OwnedImage> owned(
        reinterpret_cast<OwnedImage*>(surface.texture_handle));
    if (owned->image) vkDestroyImage(owned->device, owned->image, nullptr);
    if (owned->memory) vkFreeMemory(owned->device, owned->memory, nullptr);
  };
  provider.record_pass = [state](const NativeEffectPass& pass,
                                 std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(state->mutex);
    const VkImage input = resolve_image(pass.input);
    const VkImage output = resolve_image(pass.output);
    if (!input || !output || input == output) {
      diagnostic = "invalid Android Vulkan effect pass images";
      state->abort_recording();
      return false;
    }
    if (!state->begin(diagnostic)) return false;
    if (!state->dispatch(reinterpret_cast<void*>(state->command_buffer), pass,
                         reinterpret_cast<std::uint64_t>(input),
                         reinterpret_cast<std::uint64_t>(output), diagnostic)) {
      state->abort_recording();
      return false;
    }
    return true;
  };
  provider.submit = [state](std::string& diagnostic) {
    return state->submit_and_wait(diagnostic);
  };

  if (!validate_native_effect_provider(provider, diagnostic)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = std::move(diagnostic);
    return out;
  }
  out.provider = std::move(provider);
  out.lifetime = std::move(state);
  out.result = DIGITOR_RESULT_OK;
  return out;
}

}  // namespace digitor

#else

namespace digitor {
AndroidVulkanEffectProviderResult create_android_vulkan_effect_provider(
    AndroidVulkanEffectProviderBindings) noexcept {
  AndroidVulkanEffectProviderResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Android Vulkan effect provider is only available on Android";
  return out;
}
}  // namespace digitor

#endif
