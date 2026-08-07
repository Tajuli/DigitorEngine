#pragma once

#include <vulkan/vulkan.h>

// Android API 26's Vulkan loader does not guarantee direct exported symbols for
// commands promoted to Vulkan 1.1. The qualification harness enables
// VK_KHR_sampler_ycbcr_conversion, so resolve the device commands through the
// Vulkan dispatch table instead of requiring linker-visible 1.1 entrypoints.
inline VkResult digitor_vk_create_sampler_ycbcr_conversion(
    VkDevice device,
    const VkSamplerYcbcrConversionCreateInfo* create_info,
    const VkAllocationCallbacks* allocator,
    VkSamplerYcbcrConversion* conversion) {
  auto fn = reinterpret_cast<PFN_vkCreateSamplerYcbcrConversionKHR>(
      vkGetDeviceProcAddr(device, "vkCreateSamplerYcbcrConversionKHR"));
  if (!fn) {
    fn = reinterpret_cast<PFN_vkCreateSamplerYcbcrConversionKHR>(
        vkGetDeviceProcAddr(device, "vkCreateSamplerYcbcrConversion"));
  }
  if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
  return fn(device, create_info, allocator, conversion);
}

inline void digitor_vk_destroy_sampler_ycbcr_conversion(
    VkDevice device,
    VkSamplerYcbcrConversion conversion,
    const VkAllocationCallbacks* allocator) {
  auto fn = reinterpret_cast<PFN_vkDestroySamplerYcbcrConversionKHR>(
      vkGetDeviceProcAddr(device, "vkDestroySamplerYcbcrConversionKHR"));
  if (!fn) {
    fn = reinterpret_cast<PFN_vkDestroySamplerYcbcrConversionKHR>(
        vkGetDeviceProcAddr(device, "vkDestroySamplerYcbcrConversion"));
  }
  if (fn) fn(device, conversion, allocator);
}

#define vkCreateSamplerYcbcrConversion digitor_vk_create_sampler_ycbcr_conversion
#define vkDestroySamplerYcbcrConversion digitor_vk_destroy_sampler_ycbcr_conversion
