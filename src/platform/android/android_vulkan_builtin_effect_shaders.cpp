#include "digitor/android_vulkan_builtin_effect_shaders.hpp"

#if defined(__ANDROID__)

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace digitor {
namespace {

template <typename T>
T decode_handle(std::uint64_t value) noexcept {
  static_assert(sizeof(T) <= sizeof(value));
  T result{};
  std::memcpy(&result, &value, sizeof(T));
  return result;
}

struct PushConstants final {
  std::uint32_t effect_kind{};
  std::uint32_t pass_index{};
  std::uint32_t pass_count{};
  std::uint32_t quality{};
  float amount{};
  float radius{};
  float angle{};
  std::uint32_t format_kind{};
  std::uint32_t seed_lo{};
  std::uint32_t seed_hi{};
};

bool effect_kind(const std::string& id, std::uint32_t& value) noexcept {
  if (id == "effect.gaussian_blur") value = 0;
  else if (id == "effect.sharpen") value = 1;
  else if (id == "effect.glow") value = 2;
  else if (id == "effect.lens_distortion") value = 3;
  else if (id == "effect.noise") value = 4;
  else if (id == "effect.film_grain") value = 5;
  else if (id == "effect.chromatic_aberration") value = 6;
  else if (id == "effect.vignette") value = 7;
  else if (id == "effect.motion_blur") value = 8;
  else return false;
  return true;
}

VkFormat vk_format(NativeEffectFormat format) noexcept {
  switch (format) {
    case NativeEffectFormat::rgba8_unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case NativeEffectFormat::bgra8_unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case NativeEffectFormat::rgba16_float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
  }
}

struct PipelineVariant final {
  VkShaderModule shader{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
};

struct VulkanShaderState final {
  VkDevice device{VK_NULL_HANDLE};
  VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
  VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
  VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
  PipelineVariant sdr;
  PipelineVariant hdr;
  std::mutex mutex;

  ~VulkanShaderState() {
    if (!device) return;
    if (sdr.pipeline) vkDestroyPipeline(device, sdr.pipeline, nullptr);
    if (hdr.pipeline) vkDestroyPipeline(device, hdr.pipeline, nullptr);
    if (sdr.shader) vkDestroyShaderModule(device, sdr.shader, nullptr);
    if (hdr.shader) vkDestroyShaderModule(device, hdr.shader, nullptr);
    if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    if (descriptor_layout) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
  }
};

bool valid_code(const AndroidVulkanBuiltinEffectShaderCode& code) noexcept {
  return code.words && code.word_count >= 5 && code.words[0] == 0x07230203u;
}

bool create_shader(VkDevice device,
                   const AndroidVulkanBuiltinEffectShaderCode& code,
                   VkShaderModule& output) noexcept {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = code.word_count * sizeof(std::uint32_t);
  info.pCode = code.words;
  return vkCreateShaderModule(device, &info, nullptr, &output) == VK_SUCCESS;
}

bool create_pipeline(VkDevice device, VkPipelineLayout layout,
                     VkShaderModule shader, VkPipeline& output) noexcept {
  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = shader;
  stage.pName = "main";
  VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  info.stage = stage;
  info.layout = layout;
  return vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &output) == VK_SUCCESS;
}

VkImageView create_view(VkDevice device, VkImage image, VkFormat format) noexcept {
  VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  info.image = image;
  info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  info.format = format;
  info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  info.subresourceRange.levelCount = 1;
  info.subresourceRange.layerCount = 1;
  VkImageView view{VK_NULL_HANDLE};
  return vkCreateImageView(device, &info, nullptr, &view) == VK_SUCCESS
      ? view : VK_NULL_HANDLE;
}

void storage_barrier(VkCommandBuffer command_buffer, VkImage image) noexcept {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

}  // namespace

AndroidVulkanBuiltinEffectShadersResult
create_android_vulkan_builtin_effect_shaders(
    AndroidVulkanBuiltinEffectShadersBindings bindings) noexcept {
  AndroidVulkanBuiltinEffectShadersResult out{};
  if (!bindings.device || !valid_code(bindings.rgba8_compute_shader) ||
      !valid_code(bindings.rgba16f_compute_shader)) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Android Vulkan shader bindings or SPIR-V payloads are invalid";
    return out;
  }

  auto state = std::make_shared<VulkanShaderState>();
  state->device = reinterpret_cast<VkDevice>(bindings.device);

  std::array<VkDescriptorSetLayoutBinding, 2> descriptor_bindings{};
  for (std::uint32_t i = 0; i < descriptor_bindings.size(); ++i) {
    descriptor_bindings[i].binding = i;
    descriptor_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptor_bindings[i].descriptorCount = 1;
    descriptor_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo descriptor_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  descriptor_info.bindingCount = static_cast<std::uint32_t>(descriptor_bindings.size());
  descriptor_info.pBindings = descriptor_bindings.data();
  if (vkCreateDescriptorSetLayout(state->device, &descriptor_info, nullptr,
                                  &state->descriptor_layout) != VK_SUCCESS) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "Vulkan effect descriptor-set layout creation failed";
    return out;
  }

  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_range.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &state->descriptor_layout;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (vkCreatePipelineLayout(state->device, &layout_info, nullptr,
                             &state->pipeline_layout) != VK_SUCCESS ||
      !create_shader(state->device, bindings.rgba8_compute_shader, state->sdr.shader) ||
      !create_shader(state->device, bindings.rgba16f_compute_shader, state->hdr.shader) ||
      !create_pipeline(state->device, state->pipeline_layout, state->sdr.shader,
                       state->sdr.pipeline) ||
      !create_pipeline(state->device, state->pipeline_layout, state->hdr.shader,
                       state->hdr.pipeline)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "Vulkan built-in effect shader or pipeline creation failed";
    return out;
  }

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  pool_size.descriptorCount = 128;
  VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 64;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  if (vkCreateDescriptorPool(state->device, &pool_info, nullptr,
                             &state->descriptor_pool) != VK_SUCCESS) {
    out.result = DIGITOR_RESULT_OUT_OF_MEMORY;
    out.diagnostic = "Vulkan effect descriptor pool creation failed";
    return out;
  }

  out.dispatch = [state](void* command_buffer_handle,
                         const NativeEffectPass& pass,
                         std::uint64_t input_image_handle,
                         std::uint64_t output_image_handle,
                         std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(state->mutex);
    VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(command_buffer_handle);
    const VkImage input = decode_handle<VkImage>(input_image_handle);
    const VkImage output = decode_handle<VkImage>(output_image_handle);
    if (!command_buffer || !input || !output || input == output ||
        pass.input.format != pass.output.format ||
        pass.input.width != pass.output.width ||
        pass.input.height != pass.output.height) {
      diagnostic = "Vulkan built-in effect dispatch received incompatible resources";
      return false;
    }
    const VkFormat format = vk_format(pass.input.format);
    if (format == VK_FORMAT_UNDEFINED) {
      diagnostic = "Vulkan built-in effect format is unsupported";
      return false;
    }
    std::uint32_t kind{};
    if (!effect_kind(pass.effect.effect_id, kind)) {
      diagnostic = "unknown Vulkan built-in effect id: " + pass.effect.effect_id;
      return false;
    }

    VkImageView input_view = create_view(state->device, input, format);
    VkImageView output_view = create_view(state->device, output, format);
    if (!input_view || !output_view) {
      if (input_view) vkDestroyImageView(state->device, input_view, nullptr);
      if (output_view) vkDestroyImageView(state->device, output_view, nullptr);
      diagnostic = "Vulkan effect image-view creation failed";
      return false;
    }

    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = state->descriptor_pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &state->descriptor_layout;
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    if (vkAllocateDescriptorSets(state->device, &allocation, &descriptor_set) != VK_SUCCESS) {
      vkDestroyImageView(state->device, input_view, nullptr);
      vkDestroyImageView(state->device, output_view, nullptr);
      diagnostic = "Vulkan effect descriptor-set allocation failed";
      return false;
    }

    std::array<VkDescriptorImageInfo, 2> images{};
    images[0].imageView = input_view;
    images[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[1].imageView = output_view;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (std::uint32_t i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = descriptor_set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(state->device, static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);

    PushConstants constants{};
    constants.effect_kind = kind;
    constants.pass_index = pass.pass_index;
    constants.pass_count = pass.pass_count;
    constants.quality = static_cast<std::uint32_t>(pass.quality);
    constants.amount = pass.effect.amount;
    constants.radius = pass.effect.radius;
    constants.angle = pass.effect.angle;
    constants.format_kind = pass.input.format == NativeEffectFormat::rgba16_float ? 2u : 1u;
    constants.seed_lo = static_cast<std::uint32_t>(pass.effect.seed);
    constants.seed_hi = static_cast<std::uint32_t>(pass.effect.seed >> 32u);

    storage_barrier(command_buffer, input);
    storage_barrier(command_buffer, output);
    const VkPipeline pipeline = pass.input.format == NativeEffectFormat::rgba16_float
        ? state->hdr.pipeline : state->sdr.pipeline;
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            state->pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
    vkCmdPushConstants(command_buffer, state->pipeline_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(command_buffer, (pass.output.width + 7u) / 8u,
                  (pass.output.height + 7u) / 8u, 1u);
    storage_barrier(command_buffer, output);

    diagnostic.clear();
    return true;
  };

  out.lifetime = state;
  out.package_identity = "digitor.android.vulkan.builtin-effects.pipeline.v1";
  out.result = DIGITOR_RESULT_OK;
  return out;
}

}  // namespace digitor

#else

namespace digitor {
AndroidVulkanBuiltinEffectShadersResult
create_android_vulkan_builtin_effect_shaders(
    AndroidVulkanBuiltinEffectShadersBindings) noexcept {
  AndroidVulkanBuiltinEffectShadersResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Android Vulkan built-in effect pipelines are only available on Android";
  return out;
}
}  // namespace digitor

#endif
