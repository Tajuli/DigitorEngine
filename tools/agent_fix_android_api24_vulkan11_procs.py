# Temporary one-shot source transformer for PR #360.
from pathlib import Path

path = Path("src/gpu/vulkan_backend.cpp")
text = path.read_text(encoding="utf-8")

old_loader_block = '''    const auto import_semaphore_fd =
        reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
            vkGetDeviceProcAddr(d_, "vkImportSemaphoreFdKHR"));
    if (!get_ahb_properties || !import_semaphore_fd)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
'''
new_loader_block = '''    const auto import_semaphore_fd =
        reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
            vkGetDeviceProcAddr(d_, "vkImportSemaphoreFdKHR"));
    auto create_sampler_ycbcr_conversion =
        reinterpret_cast<PFN_vkCreateSamplerYcbcrConversionKHR>(
            vkGetDeviceProcAddr(d_, "vkCreateSamplerYcbcrConversion"));
    if (!create_sampler_ycbcr_conversion)
      create_sampler_ycbcr_conversion =
          reinterpret_cast<PFN_vkCreateSamplerYcbcrConversionKHR>(
              vkGetDeviceProcAddr(d_, "vkCreateSamplerYcbcrConversionKHR"));
    auto destroy_sampler_ycbcr_conversion =
        reinterpret_cast<PFN_vkDestroySamplerYcbcrConversionKHR>(
            vkGetDeviceProcAddr(d_, "vkDestroySamplerYcbcrConversion"));
    if (!destroy_sampler_ycbcr_conversion)
      destroy_sampler_ycbcr_conversion =
          reinterpret_cast<PFN_vkDestroySamplerYcbcrConversionKHR>(
              vkGetDeviceProcAddr(d_, "vkDestroySamplerYcbcrConversionKHR"));
    if (!get_ahb_properties || !import_semaphore_fd ||
        !create_sampler_ycbcr_conversion || !destroy_sampler_ycbcr_conversion)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
'''

old_destroy = '''      if (conversion)
        vkDestroySamplerYcbcrConversion(d_, conversion, nullptr);
'''
new_destroy = '''      if (conversion)
        destroy_sampler_ycbcr_conversion(d_, conversion, nullptr);
'''

old_create = '''    if (vkCreateSamplerYcbcrConversion(
            d_, &conversion_create, nullptr, &conversion) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
'''
new_create = '''    if (create_sampler_ycbcr_conversion(
            d_, &conversion_create, nullptr, &conversion) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
'''

old_features = '''  VkPhysicalDeviceSamplerYcbcrConversionFeatures android_ycbcr{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
  VkPhysicalDeviceFeatures2 android_features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  android_features.pNext = &android_ycbcr;
  vkGetPhysicalDeviceFeatures2(p[0], &android_features);
  if (!android_ycbcr.samplerYcbcrConversion) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
'''
new_features = '''  auto get_android_features2 =
      reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(
          vkGetInstanceProcAddr(in, "vkGetPhysicalDeviceFeatures2"));
  if (!get_android_features2)
    get_android_features2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(
            vkGetInstanceProcAddr(in, "vkGetPhysicalDeviceFeatures2KHR"));
  if (!get_android_features2) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
  VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR android_ycbcr{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR};
  VkPhysicalDeviceFeatures2KHR android_features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR};
  android_features.pNext = &android_ycbcr;
  get_android_features2(p[0], &android_features);
  if (!android_ycbcr.samplerYcbcrConversion) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
'''

replacements = [
    (old_loader_block, new_loader_block, "Android importer loader block"),
    (old_destroy, new_destroy, "YCbCr conversion destroy call"),
    (old_create, new_create, "YCbCr conversion create call"),
    (old_features, new_features, "Android feature query"),
]

for old, new, label in replacements:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    text = text.replace(old, new, 1)

for forbidden in (
    "vkGetPhysicalDeviceFeatures2(p[0], &android_features)",
    "vkCreateSamplerYcbcrConversion(\n            d_, &conversion_create",
    "vkDestroySamplerYcbcrConversion(d_, conversion, nullptr)",
):
    if forbidden in text:
        raise SystemExit(f"direct Vulkan 1.1 call still present: {forbidden}")

path.write_text(text, encoding="utf-8")
print("Patched Android Vulkan 1.1 entry points to runtime proc lookup.")
