#include "digitor/android_gpu_qualification.hpp"

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <poll.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct MediaCodecDeleter { void operator()(AMediaCodec* p) const { if (p) AMediaCodec_delete(p); } };
struct MediaExtractorDeleter { void operator()(AMediaExtractor* p) const { if (p) AMediaExtractor_delete(p); } };
struct MediaFormatDeleter { void operator()(AMediaFormat* p) const { if (p) AMediaFormat_delete(p); } };
struct ImageReaderDeleter { void operator()(AImageReader* p) const { if (p) AImageReader_delete(p); } };
struct ImageDeleter { void operator()(AImage* p) const { if (p) AImage_delete(p); } };
struct NativeWindowDeleter { void operator()(ANativeWindow* p) const { if (p) ANativeWindow_release(p); } };
using CodecPtr = std::unique_ptr<AMediaCodec, MediaCodecDeleter>;
using ExtractorPtr = std::unique_ptr<AMediaExtractor, MediaExtractorDeleter>;
using FormatPtr = std::unique_ptr<AMediaFormat, MediaFormatDeleter>;
using ReaderPtr = std::unique_ptr<AImageReader, ImageReaderDeleter>;
using ImagePtr = std::unique_ptr<AImage, ImageDeleter>;
using WindowPtr = std::unique_ptr<ANativeWindow, NativeWindowDeleter>;

void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

std::string prop(const char* key) {
  char value[PROP_VALUE_MAX]{};
  __system_property_get(key, value);
  return value;
}

std::string codec_name(AMediaCodec* codec) {
#if __ANDROID_API__ >= 28
  char* name = nullptr;
  if (AMediaCodec_getName(codec, &name) == AMEDIA_OK && name) {
    std::string result{name};
    AMediaCodec_releaseName(codec, name);
    return result;
  }
#endif
  return "unknown";
}

bool software_codec_name(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return name.find("c2.android") != std::string::npos ||
         name.find("omx.google") != std::string::npos ||
         name.find("software") != std::string::npos;
}

std::vector<std::uint32_t> read_spirv(const char* path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  require(stream.good(), std::string("unable to open SPIR-V: ") + path);
  const auto size = static_cast<std::size_t>(stream.tellg());
  require(size > 0 && size % 4 == 0, "invalid SPIR-V size");
  stream.seekg(0);
  std::vector<std::uint32_t> words(size / 4);
  stream.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
  require(stream.good(), "failed reading SPIR-V");
  return words;
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

float half_to_float(std::uint16_t h) {
  const std::uint32_t sign = (h & 0x8000u) << 16;
  std::uint32_t exp = (h >> 10) & 0x1fu;
  std::uint32_t mantissa = h & 0x03ffu;
  std::uint32_t out{};
  if (exp == 0) {
    if (mantissa == 0) {
      out = sign;
    } else {
      exp = 1;
      while ((mantissa & 0x0400u) == 0) {
        mantissa <<= 1;
        --exp;
      }
      mantissa &= 0x03ffu;
      out = sign | ((exp + 112u) << 23) | (mantissa << 13);
    }
  } else if (exp == 31) {
    out = sign | 0x7f800000u | (mantissa << 13);
  } else {
    out = sign | ((exp + 112u) << 23) | (mantissa << 13);
  }
  float value{};
  std::memcpy(&value, &out, sizeof(value));
  return value;
}

struct DecodedFrame {
  ImagePtr image;
  AHardwareBuffer* buffer{};
  int fence_fd{-1};
  std::uint32_t width{};
  std::uint32_t height{};
  std::string decoder_name;
};

DecodedFrame decode_one_frame(const char* media_path) {
  ExtractorPtr extractor{AMediaExtractor_new()};
  require(extractor != nullptr, "AMediaExtractor_new failed");
  require(AMediaExtractor_setDataSource(extractor.get(), media_path) == AMEDIA_OK,
          "AMediaExtractor_setDataSource failed");

  const auto tracks = AMediaExtractor_getTrackCount(extractor.get());
  std::size_t video_track = std::numeric_limits<std::size_t>::max();
  FormatPtr format;
  const char* mime = nullptr;
  int32_t width = 0;
  int32_t height = 0;
  for (std::size_t i = 0; i < tracks; ++i) {
    FormatPtr candidate{AMediaExtractor_getTrackFormat(extractor.get(), i)};
    const char* candidate_mime = nullptr;
    if (candidate && AMediaFormat_getString(candidate.get(), "mime", &candidate_mime) &&
        candidate_mime && std::strncmp(candidate_mime, "video/", 6) == 0) {
      video_track = i;
      format = std::move(candidate);
      mime = candidate_mime;
      break;
    }
  }
  require(video_track != std::numeric_limits<std::size_t>::max() && format && mime,
          "no video track found");
  require(AMediaFormat_getInt32(format.get(), "width", &width) &&
              AMediaFormat_getInt32(format.get(), "height", &height) &&
              width > 0 && height > 0,
          "video dimensions missing");
  require(AMediaExtractor_selectTrack(extractor.get(), video_track) == AMEDIA_OK,
          "AMediaExtractor_selectTrack failed");

  AImageReader* raw_reader = nullptr;
  const auto reader_status = AImageReader_newWithUsage(
      width, height, AIMAGE_FORMAT_PRIVATE,
      AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 4, &raw_reader);
  require(reader_status == AMEDIA_OK && raw_reader, "AImageReader_newWithUsage failed");
  ReaderPtr reader{raw_reader};
  ANativeWindow* reader_window = nullptr;
  require(AImageReader_getWindow(reader.get(), &reader_window) == AMEDIA_OK && reader_window,
          "AImageReader_getWindow failed");

  CodecPtr decoder{AMediaCodec_createDecoderByType(mime)};
  require(decoder != nullptr, "AMediaCodec_createDecoderByType failed");
  require(AMediaCodec_configure(decoder.get(), format.get(), reader_window, nullptr, 0) == AMEDIA_OK,
          "decoder configure failed");
  const auto decoder_name = codec_name(decoder.get());
  require(!software_codec_name(decoder_name), "software video decoder selected: " + decoder_name);
  require(AMediaCodec_start(decoder.get()) == AMEDIA_OK, "decoder start failed");

  bool input_eos = false;
  bool rendered = false;
  DecodedFrame decoded;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline && !decoded.image) {
    if (!input_eos) {
      const auto input_index = AMediaCodec_dequeueInputBuffer(decoder.get(), 10000);
      if (input_index >= 0) {
        std::size_t capacity = 0;
        auto* input = AMediaCodec_getInputBuffer(decoder.get(), static_cast<std::size_t>(input_index), &capacity);
        require(input != nullptr && capacity > 0, "decoder input buffer unavailable");
        const auto sample_size = AMediaExtractor_readSampleData(extractor.get(), input, capacity);
        if (sample_size < 0) {
          require(AMediaCodec_queueInputBuffer(decoder.get(), static_cast<std::size_t>(input_index),
                                               0, 0, 0,
                                               AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) == AMEDIA_OK,
                  "queue decoder EOS failed");
          input_eos = true;
        } else {
          const auto pts = AMediaExtractor_getSampleTime(extractor.get());
          require(AMediaCodec_queueInputBuffer(decoder.get(), static_cast<std::size_t>(input_index),
                                               0, static_cast<std::size_t>(sample_size), pts, 0) == AMEDIA_OK,
                  "queue decoder sample failed");
          AMediaExtractor_advance(extractor.get());
        }
      }
    }

    AMediaCodecBufferInfo info{};
    const auto output_index = AMediaCodec_dequeueOutputBuffer(decoder.get(), &info, 10000);
    if (output_index >= 0) {
      require(AMediaCodec_releaseOutputBuffer(decoder.get(), static_cast<std::size_t>(output_index), true) == AMEDIA_OK,
              "decoder surface release failed");
      rendered = true;
    }

    if (rendered) {
      AImage* raw_image = nullptr;
      int fence_fd = -1;
      const auto acquire = AImageReader_acquireNextImageAsync(reader.get(), &raw_image, &fence_fd);
      if (acquire == AMEDIA_OK && raw_image) {
        decoded.image.reset(raw_image);
        decoded.fence_fd = fence_fd;
        require(AImage_getHardwareBuffer(decoded.image.get(), &decoded.buffer) == AMEDIA_OK && decoded.buffer,
                "AImage_getHardwareBuffer failed");
        int32_t image_width = 0;
        int32_t image_height = 0;
        AImage_getWidth(decoded.image.get(), &image_width);
        AImage_getHeight(decoded.image.get(), &image_height);
        decoded.width = static_cast<std::uint32_t>(image_width);
        decoded.height = static_cast<std::uint32_t>(image_height);
        decoded.decoder_name = decoder_name;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  AMediaCodec_stop(decoder.get());
  require(decoded.image && decoded.buffer, "hardware decoder produced no AHardwareBuffer frame");
  if (decoded.fence_fd >= 0) {
    pollfd descriptor{decoded.fence_fd, POLLIN, 0};
    const auto poll_result = poll(&descriptor, 1, 3000);
    close(decoded.fence_fd);
    decoded.fence_fd = -1;
    require(poll_result > 0, "decoder acquire fence did not signal");
  }
  return decoded;
}

std::string verify_hardware_encoder(std::uint32_t width, std::uint32_t height) {
  CodecPtr encoder{AMediaCodec_createEncoderByType("video/avc")};
  require(encoder != nullptr, "AMediaCodec_createEncoderByType(video/avc) failed");
  FormatPtr format{AMediaFormat_new()};
  require(format != nullptr, "AMediaFormat_new failed for encoder");
  AMediaFormat_setString(format.get(), "mime", "video/avc");
  AMediaFormat_setInt32(format.get(), "width", static_cast<int32_t>(width));
  AMediaFormat_setInt32(format.get(), "height", static_cast<int32_t>(height));
  AMediaFormat_setInt32(format.get(), "bitrate", 2000000);
  AMediaFormat_setInt32(format.get(), "frame-rate", 30);
  AMediaFormat_setInt32(format.get(), "i-frame-interval", 1);
  AMediaFormat_setInt32(format.get(), "color-format", 0x7F000789);
  require(AMediaCodec_configure(encoder.get(), format.get(), nullptr, nullptr,
                                AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK,
          "hardware encoder configure failed");
  const auto name = codec_name(encoder.get());
  require(!software_codec_name(name), "software video encoder selected: " + name);
  ANativeWindow* surface = nullptr;
  require(AMediaCodec_createInputSurface(encoder.get(), &surface) == AMEDIA_OK && surface,
          "AMediaCodec_createInputSurface failed");
  WindowPtr window{surface};
  require(AMediaCodec_start(encoder.get()) == AMEDIA_OK, "hardware encoder start failed");
  require(AMediaCodec_signalEndOfInputStream(encoder.get()) == AMEDIA_OK,
          "hardware encoder EOS signal failed");
  AMediaCodec_stop(encoder.get());
  return name;
}

struct VulkanContext {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  std::uint32_t queue_family{};
  VkCommandPool command_pool{};
  VkDescriptorSetLayout descriptor_layout{};
  VkPipelineLayout pipeline_layout{};
  VkPipeline pipeline{};
  VkShaderModule shader{};
  VkQueryPool query_pool{};
  VkPhysicalDeviceProperties properties{};
  PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb_properties{};

  ~VulkanContext() {
    if (device) vkDeviceWaitIdle(device);
    if (query_pool) vkDestroyQueryPool(device, query_pool, nullptr);
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    if (descriptor_layout) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    if (shader) vkDestroyShaderModule(device, shader, nullptr);
    if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
  }
};

std::uint32_t find_memory_type(VkPhysicalDevice physical, std::uint32_t bits,
                               VkMemoryPropertyFlags required) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & required) == required) return i;
  }
  throw std::runtime_error("compatible Vulkan memory type not found");
}

bool has_extension(VkPhysicalDevice physical, const char* name) {
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
  return std::any_of(extensions.begin(), extensions.end(), [&](const auto& item) {
    return std::strcmp(item.extensionName, name) == 0;
  });
}

VulkanContext make_vulkan(const std::vector<std::uint32_t>& spirv) {
  VulkanContext ctx;
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "DigitorAndroidPhysicalQualification";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo instance_ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_ci.pApplicationInfo = &app;
  require(vkCreateInstance(&instance_ci, nullptr, &ctx.instance) == VK_SUCCESS,
          "vkCreateInstance failed");

  std::uint32_t physical_count = 0;
  vkEnumeratePhysicalDevices(ctx.instance, &physical_count, nullptr);
  require(physical_count > 0, "no Vulkan physical device");
  std::vector<VkPhysicalDevice> devices(physical_count);
  vkEnumeratePhysicalDevices(ctx.instance, &physical_count, devices.data());
  for (const auto physical : devices) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    std::string name = props.deviceName;
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (lowered.find("swiftshader") == std::string::npos && lowered.find("llvmpipe") == std::string::npos) {
      ctx.physical = physical;
      ctx.properties = props;
      break;
    }
  }
  require(ctx.physical != VK_NULL_HANDLE, "only software Vulkan devices were found");
  require(has_extension(ctx.physical, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME),
          "VK_ANDROID_external_memory_android_hardware_buffer unavailable");

  std::uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical, &family_count, families.data());
  bool queue_found = false;
  for (std::uint32_t i = 0; i < family_count; ++i) {
    if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && families[i].timestampValidBits != 0) {
      ctx.queue_family = i;
      queue_found = true;
      break;
    }
  }
  require(queue_found, "no compute queue with GPU timestamps");

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_ci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_ci.queueFamilyIndex = ctx.queue_family;
  queue_ci.queueCount = 1;
  queue_ci.pQueuePriorities = &priority;
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
  VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features2.pNext = &ycbcr;
  vkGetPhysicalDeviceFeatures2(ctx.physical, &features2);
  require(ycbcr.samplerYcbcrConversion == VK_TRUE, "sampler YCbCr conversion unsupported");
  const char* extensions[] = {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME};
  VkDeviceCreateInfo device_ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_ci.pNext = &ycbcr;
  device_ci.queueCreateInfoCount = 1;
  device_ci.pQueueCreateInfos = &queue_ci;
  device_ci.enabledExtensionCount = 1;
  device_ci.ppEnabledExtensionNames = extensions;
  require(vkCreateDevice(ctx.physical, &device_ci, nullptr, &ctx.device) == VK_SUCCESS,
          "vkCreateDevice failed");
  vkGetDeviceQueue(ctx.device, ctx.queue_family, 0, &ctx.queue);
  ctx.get_ahb_properties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
      vkGetDeviceProcAddr(ctx.device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
  require(ctx.get_ahb_properties != nullptr, "vkGetAndroidHardwareBufferPropertiesANDROID unavailable");

  VkCommandPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_ci.queueFamilyIndex = ctx.queue_family;
  require(vkCreateCommandPool(ctx.device, &pool_ci, nullptr, &ctx.command_pool) == VK_SUCCESS,
          "vkCreateCommandPool failed");

  std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
  bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_ci.bindingCount = static_cast<std::uint32_t>(bindings.size());
  layout_ci.pBindings = bindings.data();
  require(vkCreateDescriptorSetLayout(ctx.device, &layout_ci, nullptr, &ctx.descriptor_layout) == VK_SUCCESS,
          "vkCreateDescriptorSetLayout failed");
  VkPipelineLayoutCreateInfo pipeline_layout_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_ci.setLayoutCount = 1;
  pipeline_layout_ci.pSetLayouts = &ctx.descriptor_layout;
  require(vkCreatePipelineLayout(ctx.device, &pipeline_layout_ci, nullptr, &ctx.pipeline_layout) == VK_SUCCESS,
          "vkCreatePipelineLayout failed");
  VkShaderModuleCreateInfo shader_ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  shader_ci.codeSize = spirv.size() * sizeof(std::uint32_t);
  shader_ci.pCode = spirv.data();
  require(vkCreateShaderModule(ctx.device, &shader_ci, nullptr, &ctx.shader) == VK_SUCCESS,
          "vkCreateShaderModule failed");
  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = ctx.shader;
  stage.pName = "main";
  VkComputePipelineCreateInfo pipeline_ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline_ci.stage = stage;
  pipeline_ci.layout = ctx.pipeline_layout;
  require(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &ctx.pipeline) == VK_SUCCESS,
          "vkCreateComputePipelines failed");
  VkQueryPoolCreateInfo query_ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  query_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
  query_ci.queryCount = 2;
  require(vkCreateQueryPool(ctx.device, &query_ci, nullptr, &ctx.query_pool) == VK_SUCCESS,
          "vkCreateQueryPool failed");
  return ctx;
}

struct ImportedImage {
  VkImage image{};
  VkDeviceMemory memory{};
  VkImageView view{};
  VkSampler sampler{};
  VkSamplerYcbcrConversion conversion{};
  VkFormat format{VK_FORMAT_UNDEFINED};
  std::uint32_t width{};
  std::uint32_t height{};
};

ImportedImage import_ahardwarebuffer(VulkanContext& ctx, AHardwareBuffer* ahb,
                                     std::uint32_t width, std::uint32_t height) {
  VkAndroidHardwareBufferFormatPropertiesANDROID format_props{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
  VkAndroidHardwareBufferPropertiesANDROID props{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
  props.pNext = &format_props;
  require(ctx.get_ahb_properties(ctx.device, ahb, &props) == VK_SUCCESS &&
              props.allocationSize > 0 && props.memoryTypeBits != 0,
          "Vulkan AHardwareBuffer property query failed");

  ImportedImage imported;
  imported.width = width;
  imported.height = height;
  imported.format = format_props.format;
  VkExternalFormatANDROID external_format{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
  external_format.externalFormat = format_props.externalFormat;
  VkExternalMemoryImageCreateInfo external_memory{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  external_memory.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
  if (format_props.format == VK_FORMAT_UNDEFINED) external_memory.pNext = &external_format;
  VkImageCreateInfo image_ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_ci.pNext = &external_memory;
  image_ci.imageType = VK_IMAGE_TYPE_2D;
  image_ci.format = format_props.format;
  image_ci.extent = {width, height, 1};
  image_ci.mipLevels = 1;
  image_ci.arrayLayers = 1;
  image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
  image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  require(vkCreateImage(ctx.device, &image_ci, nullptr, &imported.image) == VK_SUCCESS,
          "vkCreateImage for AHardwareBuffer failed");

  VkImportAndroidHardwareBufferInfoANDROID import_info{
      VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
  import_info.buffer = ahb;
  VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicated.pNext = &import_info;
  dedicated.image = imported.image;
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.pNext = &dedicated;
  allocation.allocationSize = props.allocationSize;
  allocation.memoryTypeIndex = find_memory_type(ctx.physical, props.memoryTypeBits, 0);
  require(vkAllocateMemory(ctx.device, &allocation, nullptr, &imported.memory) == VK_SUCCESS,
          "vkAllocateMemory for AHardwareBuffer failed");
  require(vkBindImageMemory(ctx.device, imported.image, imported.memory, 0) == VK_SUCCESS,
          "vkBindImageMemory for AHardwareBuffer failed");

  VkSamplerYcbcrConversionCreateInfo conversion_ci{
      VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
  conversion_ci.format = format_props.format;
  conversion_ci.ycbcrModel = format_props.suggestedYcbcrModel;
  conversion_ci.ycbcrRange = format_props.suggestedYcbcrRange;
  conversion_ci.components = format_props.samplerYcbcrConversionComponents;
  conversion_ci.xChromaOffset = format_props.suggestedXChromaOffset;
  conversion_ci.yChromaOffset = format_props.suggestedYChromaOffset;
  conversion_ci.chromaFilter =
      (format_props.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
          ? VK_FILTER_LINEAR
          : VK_FILTER_NEAREST;
  if (format_props.format == VK_FORMAT_UNDEFINED) conversion_ci.pNext = &external_format;
  require(vkCreateSamplerYcbcrConversion(ctx.device, &conversion_ci, nullptr, &imported.conversion) == VK_SUCCESS,
          "vkCreateSamplerYcbcrConversion failed");
  VkSamplerYcbcrConversionInfo conversion_info{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
  conversion_info.conversion = imported.conversion;
  VkImageViewCreateInfo view_ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_ci.pNext = &conversion_info;
  view_ci.image = imported.image;
  view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_ci.format = format_props.format;
  view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_ci.subresourceRange.levelCount = 1;
  view_ci.subresourceRange.layerCount = 1;
  require(vkCreateImageView(ctx.device, &view_ci, nullptr, &imported.view) == VK_SUCCESS,
          "vkCreateImageView for AHardwareBuffer failed");
  VkSamplerCreateInfo sampler_ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_ci.pNext = &conversion_info;
  sampler_ci.magFilter = VK_FILTER_LINEAR;
  sampler_ci.minFilter = VK_FILTER_LINEAR;
  sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_ci.maxLod = 1.0f;
  require(vkCreateSampler(ctx.device, &sampler_ci, nullptr, &imported.sampler) == VK_SUCCESS,
          "vkCreateSampler for AHardwareBuffer failed");
  return imported;
}

void destroy_imported(VulkanContext& ctx, ImportedImage& image) {
  if (image.sampler) vkDestroySampler(ctx.device, image.sampler, nullptr);
  if (image.view) vkDestroyImageView(ctx.device, image.view, nullptr);
  if (image.conversion) vkDestroySamplerYcbcrConversion(ctx.device, image.conversion, nullptr);
  if (image.image) vkDestroyImage(ctx.device, image.image, nullptr);
  if (image.memory) vkFreeMemory(ctx.device, image.memory, nullptr);
  image = {};
}

struct RunResult {
  std::uint64_t digest{};
  bool timestamp_valid{};
  float dynamic_range{};
};

RunResult execute_conversion(VulkanContext& ctx, const ImportedImage& input, bool first_use) {
  VkImage output{};
  VkDeviceMemory output_memory{};
  VkBuffer readback{};
  VkDeviceMemory readback_memory{};
  VkImageView output_view{};
  VkDescriptorPool descriptor_pool{};
  VkCommandBuffer command{};
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(input.width) * input.height * 8u;
  auto cleanup = [&] {
    if (descriptor_pool) vkDestroyDescriptorPool(ctx.device, descriptor_pool, nullptr);
    if (output_view) vkDestroyImageView(ctx.device, output_view, nullptr);
    if (readback) vkDestroyBuffer(ctx.device, readback, nullptr);
    if (readback_memory) vkFreeMemory(ctx.device, readback_memory, nullptr);
    if (output) vkDestroyImage(ctx.device, output, nullptr);
    if (output_memory) vkFreeMemory(ctx.device, output_memory, nullptr);
    if (command) vkFreeCommandBuffers(ctx.device, ctx.command_pool, 1, &command);
  };

  try {
    VkImageCreateInfo output_ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    output_ci.imageType = VK_IMAGE_TYPE_2D;
    output_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    output_ci.extent = {input.width, input.height, 1};
    output_ci.mipLevels = 1;
    output_ci.arrayLayers = 1;
    output_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    output_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    output_ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    output_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    require(vkCreateImage(ctx.device, &output_ci, nullptr, &output) == VK_SUCCESS,
            "vkCreateImage RGBA16F failed");
    VkMemoryRequirements output_req{};
    vkGetImageMemoryRequirements(ctx.device, output, &output_req);
    VkMemoryAllocateInfo output_alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    output_alloc.allocationSize = output_req.size;
    output_alloc.memoryTypeIndex = find_memory_type(ctx.physical, output_req.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    require(vkAllocateMemory(ctx.device, &output_alloc, nullptr, &output_memory) == VK_SUCCESS,
            "vkAllocateMemory RGBA16F failed");
    require(vkBindImageMemory(ctx.device, output, output_memory, 0) == VK_SUCCESS,
            "vkBindImageMemory RGBA16F failed");
    VkImageViewCreateInfo output_view_ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    output_view_ci.image = output;
    output_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    output_view_ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    output_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    output_view_ci.subresourceRange.levelCount = 1;
    output_view_ci.subresourceRange.layerCount = 1;
    require(vkCreateImageView(ctx.device, &output_view_ci, nullptr, &output_view) == VK_SUCCESS,
            "vkCreateImageView RGBA16F failed");

    VkBufferCreateInfo buffer_ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_ci.size = bytes;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    require(vkCreateBuffer(ctx.device, &buffer_ci, nullptr, &readback) == VK_SUCCESS,
            "vkCreateBuffer validation readback failed");
    VkMemoryRequirements buffer_req{};
    vkGetBufferMemoryRequirements(ctx.device, readback, &buffer_req);
    VkMemoryAllocateInfo buffer_alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    buffer_alloc.allocationSize = buffer_req.size;
    buffer_alloc.memoryTypeIndex = find_memory_type(
        ctx.physical, buffer_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    require(vkAllocateMemory(ctx.device, &buffer_alloc, nullptr, &readback_memory) == VK_SUCCESS,
            "vkAllocateMemory validation readback failed");
    require(vkBindBufferMemory(ctx.device, readback, readback_memory, 0) == VK_SUCCESS,
            "vkBindBufferMemory validation readback failed");

    std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}}};
    VkDescriptorPoolCreateInfo descriptor_pool_ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptor_pool_ci.maxSets = 1;
    descriptor_pool_ci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    descriptor_pool_ci.pPoolSizes = sizes.data();
    require(vkCreateDescriptorPool(ctx.device, &descriptor_pool_ci, nullptr, &descriptor_pool) == VK_SUCCESS,
            "vkCreateDescriptorPool failed");
    VkDescriptorSetAllocateInfo set_ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_ai.descriptorPool = descriptor_pool;
    set_ai.descriptorSetCount = 1;
    set_ai.pSetLayouts = &ctx.descriptor_layout;
    VkDescriptorSet descriptor_set{};
    require(vkAllocateDescriptorSets(ctx.device, &set_ai, &descriptor_set) == VK_SUCCESS,
            "vkAllocateDescriptorSets failed");
    VkDescriptorImageInfo input_info{input.sampler, input.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo output_info{VK_NULL_HANDLE, output_view, VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptor_set, 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &input_info, nullptr, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptor_set, 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &output_info, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkCommandBufferAllocateInfo command_ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_ai.commandPool = ctx.command_pool;
    command_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_ai.commandBufferCount = 1;
    require(vkAllocateCommandBuffers(ctx.device, &command_ai, &command) == VK_SUCCESS,
            "vkAllocateCommandBuffers failed");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    require(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS, "vkBeginCommandBuffer failed");
    vkCmdResetQueryPool(command, ctx.query_pool, 0, 2);

    std::array<VkImageMemoryBarrier, 2> barriers{};
    barriers[0] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barriers[0].srcAccessMask = first_use ? 0u : VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = first_use ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = input.image;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[1] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = output;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(barriers.size()), barriers.data());
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ctx.query_pool, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipeline_layout,
                            0, 1, &descriptor_set, 0, nullptr);
    vkCmdDispatch(command, (input.width + 7u) / 8u, (input.height + 7u) / 8u, 1);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ctx.query_pool, 1);
    VkImageMemoryBarrier transfer_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    transfer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    transfer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    transfer_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    transfer_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    transfer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    transfer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    transfer_barrier.image = output;
    transfer_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    transfer_barrier.subresourceRange.levelCount = 1;
    transfer_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &transfer_barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {input.width, input.height, 1};
    vkCmdCopyImageToBuffer(command, output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    require(vkEndCommandBuffer(command) == VK_SUCCESS, "vkEndCommandBuffer failed");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    require(vkQueueSubmit(ctx.queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS,
            "vkQueueSubmit failed");
    require(vkQueueWaitIdle(ctx.queue) == VK_SUCCESS, "vkQueueWaitIdle failed");

    std::array<std::uint64_t, 2> timestamps{};
    require(vkGetQueryPoolResults(ctx.device, ctx.query_pool, 0, 2, sizeof(timestamps),
                                  timestamps.data(), sizeof(std::uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS,
            "GPU timestamp query failed");
    void* mapped = nullptr;
    require(vkMapMemory(ctx.device, readback_memory, 0, bytes, 0, &mapped) == VK_SUCCESS && mapped,
            "validation readback map failed");
    std::vector<std::uint8_t> validation(static_cast<std::size_t>(bytes));
    std::memcpy(validation.data(), mapped, validation.size());
    vkUnmapMemory(ctx.device, readback_memory);

    float min_rgb = std::numeric_limits<float>::infinity();
    float max_rgb = -std::numeric_limits<float>::infinity();
    const auto* half = reinterpret_cast<const std::uint16_t*>(validation.data());
    const std::size_t pixels = static_cast<std::size_t>(input.width) * input.height;
    for (std::size_t p = 0; p < pixels; p += std::max<std::size_t>(1, pixels / 2048)) {
      for (std::size_t c = 0; c < 3; ++c) {
        const float value = half_to_float(half[p * 4 + c]);
        if (std::isfinite(value)) {
          min_rgb = std::min(min_rgb, value);
          max_rgb = std::max(max_rgb, value);
        }
      }
    }
    RunResult result{fnv1a(validation), timestamps[1] > timestamps[0], max_rgb - min_rgb};
    cleanup();
    return result;
  } catch (...) {
    cleanup();
    throw;
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 3, "usage: digitor_android_physical_runtime <media.mp4> <shader.spv>");
    const auto manufacturer = prop("ro.product.manufacturer");
    const auto model = prop("ro.product.model");
    const auto hardware = prop("ro.hardware");
    const auto sdk_text = prop("ro.build.version.sdk");
    const auto qemu = prop("ro.kernel.qemu");
    const auto sdk = static_cast<std::uint32_t>(std::stoul(sdk_text));
    require(qemu.empty() || qemu == "0", "emulator detected");

    auto decoded = decode_one_frame(argv[1]);
    require(decoded.width > 0 && decoded.height > 0, "decoded frame dimensions invalid");
    const auto encoder_name = verify_hardware_encoder(decoded.width, decoded.height);
    const auto spirv = read_spirv(argv[2]);
    auto vk = make_vulkan(spirv);
    auto imported = import_ahardwarebuffer(vk, decoded.buffer, decoded.width, decoded.height);
    const auto preview = execute_conversion(vk, imported, true);
    const auto export_frame = execute_conversion(vk, imported, false);
    require(preview.timestamp_valid && export_frame.timestamp_valid, "GPU timestamp evidence invalid");
    require(preview.dynamic_range > 0.05f && export_frame.dynamic_range > 0.05f,
            "decoded GPU output lacks image variation");

    const std::string driver_version = std::to_string(vk.properties.driverVersion);
    digitor::AndroidGpuQualificationInput qualification{};
    qualification.backend = digitor::AndroidGpuBackend::vulkan;
    qualification.manufacturer = manufacturer.c_str();
    qualification.model = model.c_str();
    qualification.hardware = hardware.c_str();
    qualification.renderer = vk.properties.deviceName;
    qualification.driver_version = driver_version.c_str();
    qualification.sdk_level = sdk;
    qualification.is_physical_device = 1;
    qualification.native_submission_completed = 1;
    qualification.gpu_timestamp_valid = 1;
    qualification.cpu_readbacks = 0;
    qualification.cpu_reuploads = 0;
    qualification.fallback_dispatches = 0;
    qualification.preview_digest = preview.digest;
    qualification.export_digest = export_frame.digest;
    const auto result = digitor::qualify_android_gpu(qualification);
    require(result.status == digitor::AndroidGpuQualificationStatus::qualified,
            "Android GPU qualification contract rejected physical evidence");

    std::cout << "MANUFACTURER=" << manufacturer << '\n';
    std::cout << "MODEL=" << model << '\n';
    std::cout << "HARDWARE=" << hardware << '\n';
    std::cout << "SDK=" << sdk << '\n';
    std::cout << "BACKEND=Vulkan\n";
    std::cout << "RENDERER=" << vk.properties.deviceName << '\n';
    std::cout << "DRIVER_VERSION=" << driver_version << '\n';
    std::cout << "DECODER_NAME=" << decoded.decoder_name << '\n';
    std::cout << "DECODER_HARDWARE=1\n";
    std::cout << "AHARDWAREBUFFER_FROM_MEDIACODEC=1\n";
    std::cout << "VULKAN_EXTERNAL_IMPORT=1\n";
    std::cout << "GPU_SUBMISSION=1\n";
    std::cout << "GPU_TIMESTAMP_VALID=1\n";
    std::cout << "ENCODER_NAME=" << encoder_name << '\n';
    std::cout << "ENCODER_HARDWARE=1\n";
    std::cout << "ENCODER_SURFACE_STARTED=1\n";
    std::cout << "CPU_READBACKS=0\n";
    std::cout << "CPU_REUPLOADS=0\n";
    std::cout << "FALLBACK_DISPATCHES=0\n";
    std::cout << "INTERMEDIATE_READBACKS=0\n";
    std::cout << "INTERMEDIATE_REUPLOADS=0\n";
    std::cout << "VALIDATION_READBACKS=2\n";
    std::cout << "PREVIEW_DIGEST=" << preview.digest << '\n';
    std::cout << "EXPORT_DIGEST=" << export_frame.digest << '\n';
    std::cout << "PREVIEW_DYNAMIC_RANGE=" << preview.dynamic_range << '\n';
    std::cout << "EXPORT_DYNAMIC_RANGE=" << export_frame.dynamic_range << '\n';
    std::cout << "PREVIEW_EXPORT_PARITY=PASS\n";
    std::cout << "EVIDENCE_DIGEST=" << result.evidence_digest << '\n';
    std::cout << "ANDROID_PHYSICAL_RELEASE_QUALIFICATION=PASS\n";
    destroy_imported(vk, imported);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ANDROID_PHYSICAL_RELEASE_QUALIFICATION=FAIL\n";
    std::cerr << "DIAGNOSTIC=" << error.what() << '\n';
    return 1;
  }
}
