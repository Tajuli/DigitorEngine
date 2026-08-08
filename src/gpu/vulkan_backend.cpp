#include <algorithm>
#include "color_pipeline_shader.hpp"
#include "core/numeric_utils.hpp"
#include "core/string_utils.hpp"
#include "digitor/shader.hpp"
#include "gpu/gpu_backend.hpp"
#include "gpu/native_pipeline_cache.hpp"
#include "gpu/native_primary_wheels.hpp"
#include "gpu/native_log_wheels.hpp"
#include "gpu/native_hsl_qualifier.hpp"
#include "gpu/native_rgb_curves.hpp"
#include "digitor/native_node_mask_backend.hpp"
#include "digitor/native_node_platform_factories.hpp"
#include "digitor/native_node_shader_contracts.hpp"
#include "primary_wheels_shader.hpp"
#include "log_wheels_shader.hpp"
#include "hsl_qualifier_shader.hpp"
#include "rgb_curves_shader.hpp"

#ifdef DIGITOR_EMBEDDED_VULKAN_SPIRV
#include "primary_wheels_texture.hpp"
#include "log_wheels_texture.hpp"
#include "hsl_qualifier_texture.hpp"
#include "rgb_curves_texture.hpp"
#include "rgb_curves_buffer.hpp"
#include "color_pipeline_buffer.hpp"
#endif
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
namespace digitor {
namespace {
struct VulkanLiveResources {
  std::atomic<std::int64_t> images{}, memory{}, views{}, buffers{},
      descriptor_pools{}, descriptor_sets{}, command_buffers{}, pipelines{},
      consumers{};
  NativeResourceCounts snapshot() const noexcept {
    NativeResourceCounts value;
    value.images = images.load();
    value.memory_allocations = memory.load();
    value.image_views = views.load();
    value.buffers = buffers.load();
    value.descriptor_pools = descriptor_pools.load();
    value.descriptor_sets = descriptor_sets.load();
    value.command_resources = command_buffers.load();
    value.pipelines = pipelines.load();
    value.consumer_destinations = consumers.load();
    return value;
  }
} vk_live;
std::mutex vk_descriptor_mutex;
std::unordered_map<VkDescriptorPool, std::uint32_t> vk_descriptor_counts;

#ifdef DIGITOR_EMBEDDED_VULKAN_SPIRV
ShaderCompileResult embedded_vulkan_shader(const ShaderCompileRequest &request) {
  const std::uint32_t *words = nullptr;
  std::size_t word_count = 0;
  const bool texture = std::any_of(
      request.macros.begin(), request.macros.end(), [](const ShaderMacro &macro) {
        return macro.name == "DIGITOR_TEXTURE_OUTPUT" && macro.value == "1";
      });
  if (request.source_name == "primary_wheels.hlsl" && texture) {
    words = digitor_primary_wheels_texture_spirv;
    word_count = digitor_primary_wheels_texture_spirv_word_count;
  } else if (request.source_name == "log_wheels.hlsl" && texture) {
    words = digitor_log_wheels_texture_spirv;
    word_count = digitor_log_wheels_texture_spirv_word_count;
  } else if (request.source_name == "hsl_qualifier.hlsl" && texture) {
    words = digitor_hsl_qualifier_texture_spirv;
    word_count = digitor_hsl_qualifier_texture_spirv_word_count;
  } else if (request.source_name == "rgb_curves.hlsl" && texture) {
    words = digitor_rgb_curves_texture_spirv;
    word_count = digitor_rgb_curves_texture_spirv_word_count;
  } else if (request.source_name == "rgb_curves.hlsl") {
    words = digitor_rgb_curves_buffer_spirv;
    word_count = digitor_rgb_curves_buffer_spirv_word_count;
  } else if (request.source_name == "color_pipeline.hlsl") {
    words = digitor_color_pipeline_buffer_spirv;
    word_count = digitor_color_pipeline_buffer_spirv_word_count;
  }
  ShaderCompileResult result;
  if (!words || !word_count) {
    result.error = ShaderError::compile_failure;
    result.diagnostics = "no embedded Vulkan shader matches " + request.source_name;
    return result;
  }
  result.error = ShaderError::none;
  result.format = ShaderBinaryFormat::spirv;
  result.compiler_identity = "Android NDK glslc (build-time)";
  result.compiler_version = "embedded";
  result.target_profile = request.target_profile;
  result.cache_key = "embedded-vulkan-spv-v1:" + request.source_name +
                     (texture ? ":texture" : ":buffer");
  result.reflection.stage = request.stage;
  result.reflection.entry_point = request.entry_point;
  result.binary.resize(word_count * sizeof(std::uint32_t));
  std::memcpy(result.binary.data(), words, result.binary.size());
  result.diagnostics = "embedded build-time SPIR-V";
  return result;
}
#endif

VkResult tracked_vkCreateImage(VkDevice d, const VkImageCreateInfo *c,
                               const VkAllocationCallbacks *a, VkImage *out) {
  auto r = vkCreateImage(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.images;
  return r;
}
void tracked_vkDestroyImage(VkDevice d, VkImage x,
                            const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyImage(d, x, a);
    --vk_live.images;
  }
}
VkResult tracked_vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *c,
                                  const VkAllocationCallbacks *a,
                                  VkDeviceMemory *out) {
  auto r = vkAllocateMemory(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.memory;
  return r;
}
void tracked_vkFreeMemory(VkDevice d, VkDeviceMemory x,
                          const VkAllocationCallbacks *a) {
  if (x) {
    vkFreeMemory(d, x, a);
    --vk_live.memory;
  }
}
VkResult tracked_vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *c,
                                   const VkAllocationCallbacks *a,
                                   VkImageView *out) {
  auto r = vkCreateImageView(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.views;
  return r;
}
void tracked_vkDestroyImageView(VkDevice d, VkImageView x,
                                const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyImageView(d, x, a);
    --vk_live.views;
  }
}
VkResult tracked_vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *c,
                                const VkAllocationCallbacks *a, VkBuffer *out) {
  auto r = vkCreateBuffer(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.buffers;
  return r;
}
void tracked_vkDestroyBuffer(VkDevice d, VkBuffer x,
                             const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyBuffer(d, x, a);
    --vk_live.buffers;
  }
}
VkResult tracked_vkCreateDescriptorPool(VkDevice d,
                                        const VkDescriptorPoolCreateInfo *c,
                                        const VkAllocationCallbacks *a,
                                        VkDescriptorPool *out) {
  auto r = vkCreateDescriptorPool(d, c, a, out);
  if (r == VK_SUCCESS) {
    ++vk_live.descriptor_pools;
    std::scoped_lock lock(vk_descriptor_mutex);
    vk_descriptor_counts[*out] = 0;
  }
  return r;
}
void tracked_vkDestroyDescriptorPool(VkDevice d, VkDescriptorPool x,
                                     const VkAllocationCallbacks *a) {
  if (x) {
    std::uint32_t sets = 0;
    {
      std::scoped_lock lock(vk_descriptor_mutex);
      auto i = vk_descriptor_counts.find(x);
      if (i != vk_descriptor_counts.end()) {
        sets = i->second;
        vk_descriptor_counts.erase(i);
      }
    }
    vkDestroyDescriptorPool(d, x, a);
    --vk_live.descriptor_pools;
    vk_live.descriptor_sets -= sets;
  }
}
VkResult tracked_vkAllocateDescriptorSets(VkDevice d,
                                          const VkDescriptorSetAllocateInfo *c,
                                          VkDescriptorSet *out) {
  auto r = vkAllocateDescriptorSets(d, c, out);
  if (r == VK_SUCCESS) {
    vk_live.descriptor_sets += c->descriptorSetCount;
    std::scoped_lock lock(vk_descriptor_mutex);
    vk_descriptor_counts[c->descriptorPool] += c->descriptorSetCount;
  }
  return r;
}
VkResult tracked_vkAllocateCommandBuffers(VkDevice d,
                                          const VkCommandBufferAllocateInfo *c,
                                          VkCommandBuffer *out) {
  auto r = vkAllocateCommandBuffers(d, c, out);
  if (r == VK_SUCCESS)
    vk_live.command_buffers += c->commandBufferCount;
  return r;
}
void tracked_vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n,
                                  const VkCommandBuffer *x) {
  if (n) {
    vkFreeCommandBuffers(d, p, n, x);
    vk_live.command_buffers -= n;
  }
}
VkResult tracked_vkCreateDescriptorSetLayout(
    VkDevice d, const VkDescriptorSetLayoutCreateInfo *c,
    const VkAllocationCallbacks *a, VkDescriptorSetLayout *out) {
  auto r = vkCreateDescriptorSetLayout(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.pipelines;
  return r;
}
void tracked_vkDestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout x,
                                          const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyDescriptorSetLayout(d, x, a);
    --vk_live.pipelines;
  }
}
VkResult tracked_vkCreatePipelineLayout(VkDevice d,
                                        const VkPipelineLayoutCreateInfo *c,
                                        const VkAllocationCallbacks *a,
                                        VkPipelineLayout *out) {
  auto r = vkCreatePipelineLayout(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.pipelines;
  return r;
}
void tracked_vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout x,
                                     const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyPipelineLayout(d, x, a);
    --vk_live.pipelines;
  }
}
VkResult tracked_vkCreateShaderModule(VkDevice d,
                                      const VkShaderModuleCreateInfo *c,
                                      const VkAllocationCallbacks *a,
                                      VkShaderModule *out) {
  auto r = vkCreateShaderModule(d, c, a, out);
  if (r == VK_SUCCESS)
    ++vk_live.pipelines;
  return r;
}
void tracked_vkDestroyShaderModule(VkDevice d, VkShaderModule x,
                                   const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyShaderModule(d, x, a);
    --vk_live.pipelines;
  }
}
VkResult tracked_vkCreateComputePipelines(VkDevice d, VkPipelineCache cache,
                                          uint32_t n,
                                          const VkComputePipelineCreateInfo *c,
                                          const VkAllocationCallbacks *a,
                                          VkPipeline *out) {
  auto r = vkCreateComputePipelines(d, cache, n, c, a, out);
  if (r == VK_SUCCESS)
    vk_live.pipelines += n;
  return r;
}
void tracked_vkDestroyPipeline(VkDevice d, VkPipeline x,
                               const VkAllocationCallbacks *a) {
  if (x) {
    vkDestroyPipeline(d, x, a);
    --vk_live.pipelines;
  }
}
struct VkTex {
  VkDevice d;
  VkImage x;
  VkDeviceMemory m;
  VkImageView v;
};
struct VkBuf {
  VkDevice d;
  VkBuffer x;
  VkDeviceMemory m;
};
struct VkSamp {
  VkDevice d;
  VkSampler x;
};
struct VkPreviewOwner {
  VkDevice device{};
  VkImage source{}, output{}, preview{};
  VkDeviceMemory source_memory{}, output_memory{}, preview_memory{};
  VkImageView source_view{}, output_view{};
  VkImageLayout output_layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  std::shared_ptr<void> upstream;
  std::weak_ptr<std::atomic_bool> device_live;
  ~VkPreviewOwner() {
    auto live = device_live.lock();
    if (!device)
      return;
    if (!live || !live->load(std::memory_order_acquire)) {
      vk_live.views -= (source_view ? 1 : 0) + (output_view ? 1 : 0);
      vk_live.images -= (source ? 1 : 0) + (output ? 1 : 0) + (preview ? 1 : 0);
      vk_live.memory -= (source_memory ? 1 : 0) + (output_memory ? 1 : 0) +
                        (preview_memory ? 1 : 0);
      return;
    }
    if (source_view)
      tracked_vkDestroyImageView(device, source_view, nullptr);
    if (output_view)
      tracked_vkDestroyImageView(device, output_view, nullptr);
    for (auto i : {source, output, preview})
      if (i)
        tracked_vkDestroyImage(device, i, nullptr);
    for (auto m : {source_memory, output_memory, preview_memory})
      if (m)
        tracked_vkFreeMemory(device, m, nullptr);
  }
};

struct VkMatteOwner {
  VkDevice device{};
  VkImage image{};
  VkDeviceMemory memory{};
  VkImageView view{};
  VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  std::vector<std::shared_ptr<void>> upstream;
  std::weak_ptr<std::atomic_bool> device_live;
  ~VkMatteOwner() {
    auto live = device_live.lock();
    if (device && live && live->load(std::memory_order_acquire)) {
      if (view) tracked_vkDestroyImageView(device, view, nullptr);
      if (image) tracked_vkDestroyImage(device, image, nullptr);
      if (memory) tracked_vkFreeMemory(device, memory, nullptr);
    } else if (device) {
      vk_live.views -= view ? 1 : 0;
      vk_live.images -= image ? 1 : 0;
      vk_live.memory -= memory ? 1 : 0;
    }
  }
};
struct VkHslMatteConstants {
  float hue[4], saturation[4], luminance[4];
  float clean_black, clean_white, denoise, blur;
  std::uint32_t invert, width, height, padding;
};
static_assert(sizeof(VkHslMatteConstants) == 80);
struct VkWindowConstants {
  float center_x, center_y, width_f, height_f;
  float rotation, feather, opacity;
  std::uint32_t shape, invert, width, height, padding;
};
static_assert(sizeof(VkWindowConstants) == 48);
struct VkSizeConstants { std::uint32_t width, height; };
static_assert(sizeof(VkSizeConstants) == 8);

struct VkConsumerOwner {
  VkDevice device{};
  VkImage image{};
  VkImageView view{};
  VkDeviceMemory memory{};
  std::weak_ptr<std::atomic_bool> device_live;
  bool counted{};
  ~VkConsumerOwner() {
    auto live = device_live.lock();
    if (device && live && live->load(std::memory_order_acquire)) {
      if (view)
        tracked_vkDestroyImageView(device, view, nullptr);
      if (image)
        tracked_vkDestroyImage(device, image, nullptr);
      if (memory)
        tracked_vkFreeMemory(device, memory, nullptr);
    } else if (device) {
      vk_live.views -= view ? 1 : 0;
      vk_live.images -= image ? 1 : 0;
      vk_live.memory -= memory ? 1 : 0;
    }
    if (counted)
      --vk_live.consumers;
  }
};
struct VkPipelineOwner {
  VkDevice device{};
  VkShaderModule shader{};
  VkDescriptorSetLayout descriptors{};
  VkPipelineLayout layout{};
  VkPipeline pipeline{};
  std::weak_ptr<std::atomic_bool> device_live;
  ~VkPipelineOwner() {
    auto live = device_live.lock();
    if (!device || !live || !live->load())
      return;
    if (pipeline)
      tracked_vkDestroyPipeline(device, pipeline, nullptr);
    if (shader)
      tracked_vkDestroyShaderModule(device, shader, nullptr);
    if (layout)
      tracked_vkDestroyPipelineLayout(device, layout, nullptr);
    if (descriptors)
      tracked_vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
  }
};
class VulkanBackend final : public IRenderBackend, public NativeNodeMaskBackend {
  VkInstance in_{};
  VkPhysicalDevice ph_{};
  VkDevice d_{};
  VkQueue queue_{};
  uint32_t family_{};
  VkCommandPool pool_{};
  VkPhysicalDeviceMemoryProperties mp_{};
  ShaderCompiler shader_compiler_{};
  ShaderCache shader_cache_{};
  ShaderCompileResult vulkan_shader(const ShaderCompileRequest &request) {
#ifdef DIGITOR_EMBEDDED_VULKAN_SPIRV
    return embedded_vulkan_shader(request);
#else
    return shader_cache_.get_or_compile(shader_compiler_, request);
#endif
  }
  std::shared_ptr<std::atomic_bool> device_live_{
      std::make_shared<std::atomic_bool>(true)};
  DigitorRendererInfo i_{};
  NativePipelineCache pipeline_cache_{8};
  class QualificationScope {
  public:
    QualificationScope(VulkanBackend &backend, const char *path) noexcept
        : backend_(backend), resources_(vk_live.snapshot()),
          cache_(backend.pipeline_cache_.counters()),
          primary_(primary_wheels_reference_count()),
          curves_(cpu_curve_reference_count()) {
      backend_.begin_grade_provenance(
          DIGITOR_RENDERER_VULKAN, true, backend_.i_.device_name,
          backend_.shader_compiler_.identity().c_str(), "vulkan-native-stage",
          "vulkan-native-pipeline");
      backend_.provenance_.requested_failure_point = gpu_failure_point();
      backend_.provenance_.failure_path = path ? path : "";
      backend_.provenance_.resources_before = resources_;
      backend_.provenance_.cache_before = cache_;
    }
    ~QualificationScope() {
      auto &p = backend_.provenance_;
      p.resources_after = vk_live.snapshot();
      p.cache_after = backend_.pipeline_cache_.counters();
      auto transient_after = p.resources_after, transient_before = resources_;
      transient_after.pipelines = transient_before.pipelines = 0;
      const auto retained_pipeline_objects =
          p.resources_after.pipelines - resources_.pipelines;
      const auto retained_cache_entries =
          static_cast<std::int64_t>(p.cache_after.creations - cache_.creations);
      p.cleanup_baseline =
          p.failure_result == DIGITOR_RESULT_OK ||
          (transient_after == transient_before &&
           retained_pipeline_objects >= 0 &&
           retained_pipeline_objects <= retained_cache_entries * 4);
      p.cache_valid =
          p.cache_after.hits >= cache_.hits &&
          p.cache_after.creations >= cache_.creations &&
          p.cache_after.creation_failures >= cache_.creation_failures;
      p.cpu_primary_wheels_invocations =
          primary_wheels_reference_count() - primary_;
      p.cpu_curve_invocations = cpu_curve_reference_count() - curves_;
      p.output_cleared =
          p.failure_result == DIGITOR_RESULT_OK || !p.output_written;
      p.recovery_succeeded = p.failure_result == DIGITOR_RESULT_OK;
    }

  private:
    VulkanBackend &backend_;
    NativeResourceCounts resources_;
    NativePipelineCacheCounters cache_;
    std::uint64_t primary_, curves_;
  };
  VkResult injected_vk_result(GpuFailurePoint point,
                              const char *operation) noexcept {
    const auto result = inject_at(point, operation);
    if (result == DIGITOR_RESULT_OK)
      return VK_SUCCESS;
    return result == DIGITOR_RESULT_OUT_OF_MEMORY
               ? VK_ERROR_OUT_OF_DEVICE_MEMORY
               : VK_ERROR_UNKNOWN;
  }
  VkResult allocation_stage(GpuFailurePoint point,
                            const char *operation) noexcept {
    if (auto r = injected_vk_result(GpuFailurePoint::DeterministicOutOfMemory,
                                    operation);
        r != VK_SUCCESS)
      return r;
    return injected_vk_result(point, operation);
  }
  VkResult allocate_command_buffers(const VkCommandBufferAllocateInfo *info,
                                    VkCommandBuffer *out) noexcept {
    if (auto r =
            injected_vk_result(GpuFailurePoint::CommandBufferOrListAllocation,
                               "vkAllocateCommandBuffers");
        r != VK_SUCCESS)
      return r;
    return tracked_vkAllocateCommandBuffers(d_, info, out);
  }
  VkResult begin_command_buffer(VkCommandBuffer command,
                                const VkCommandBufferBeginInfo *info) noexcept {
    if (auto r =
            injected_vk_result(GpuFailurePoint::CommandBufferOrListBeginReset,
                               "vkBeginCommandBuffer");
        r != VK_SUCCESS)
      return r;
    const auto result = vkBeginCommandBuffer(command, info);
    if (result != VK_SUCCESS)
      return result;
    return injected_vk_result(GpuFailurePoint::CommandRecording,
                              "command recording");
  }
  void dispatch(VkCommandBuffer command, std::uint32_t x, std::uint32_t y,
                std::uint32_t z) noexcept {
    // A prior injected failure (for example ResourceBinding) leaves the
    // command buffer without a valid bound compute pipeline/descriptors.
    // Never emit vkCmdDispatch after any earlier stage has failed: some
    // Android Vulkan drivers treat that invalid command stream as a fatal
    // process error instead of reporting a validation failure.
    if (provenance_.failure_result != DIGITOR_RESULT_OK)
      return;
    if (injected_vk_result(GpuFailurePoint::DispatchOrDraw, "vkCmdDispatch") !=
        VK_SUCCESS)
      return;
    vkCmdDispatch(command, x, y, z);
  }
  VkResult end_command_buffer(VkCommandBuffer command) noexcept {
    if (provenance_.failure_result != DIGITOR_RESULT_OK)
      return VK_ERROR_UNKNOWN;
    if (auto r = injected_vk_result(GpuFailurePoint::CommandBufferOrListClose,
                                    "vkEndCommandBuffer");
        r != VK_SUCCESS)
      return r;
    return vkEndCommandBuffer(command);
  }
  VkResult queue_submit(const VkSubmitInfo *submit) noexcept {
    if (provenance_.failure_result != DIGITOR_RESULT_OK)
      return VK_ERROR_UNKNOWN;
    if (auto r = injected_vk_result(GpuFailurePoint::QueueSubmission,
                                    "vkQueueSubmit");
        r != VK_SUCCESS)
      return r;
    return vkQueueSubmit(queue_, 1, submit, VK_NULL_HANDLE);
  }
  VkResult queue_wait_idle() noexcept {
    if (provenance_.failure_result != DIGITOR_RESULT_OK)
      return VK_ERROR_UNKNOWN;
    if (auto r = injected_vk_result(GpuFailurePoint::SynchronizationWait,
                                    "vkQueueWaitIdle");
        r != VK_SUCCESS) {
      (void)vkQueueWaitIdle(queue_);
      return r;
    }
    return vkQueueWaitIdle(queue_);
  }
  VkResult create_image_view(const VkImageViewCreateInfo *info,
                             VkImageView *out) noexcept {
    if (auto r = injected_vk_result(GpuFailurePoint::ImageViewCreation,
                                    "vkCreateImageView");
        r != VK_SUCCESS)
      return r;
    return tracked_vkCreateImageView(d_, info, nullptr, out);
  }
  VkResult create_descriptor_pool(const VkDescriptorPoolCreateInfo *info,
                                  VkDescriptorPool *out) noexcept {
    if (auto r = injected_vk_result(GpuFailurePoint::DescriptorPoolCreation,
                                    "vkCreateDescriptorPool");
        r != VK_SUCCESS)
      return r;
    return tracked_vkCreateDescriptorPool(d_, info, nullptr, out);
  }
  VkResult allocate_descriptor_sets(const VkDescriptorSetAllocateInfo *info,
                                    VkDescriptorSet *out) noexcept {
    if (auto r = injected_vk_result(GpuFailurePoint::DescriptorSetAllocation,
                                    "vkAllocateDescriptorSets");
        r != VK_SUCCESS)
      return r;
    return tracked_vkAllocateDescriptorSets(d_, info, out);
  }
  bool update_descriptors(std::uint32_t count,
                          const VkWriteDescriptorSet *writes) noexcept {
    if (injected_vk_result(GpuFailurePoint::DescriptorUpdate,
                           "vkUpdateDescriptorSets") != VK_SUCCESS)
      return false;
    vkUpdateDescriptorSets(d_, count, writes, 0, nullptr);
    return true;
  }
  bool bind_compute_resources(VkCommandBuffer command, VkPipeline pipeline,
                              VkPipelineLayout layout,
                              VkDescriptorSet set) noexcept {
    if (injected_vk_result(GpuFailurePoint::ResourceBinding,
                           "vkCmdBindPipeline/vkCmdBindDescriptorSets") !=
        VK_SUCCESS)
      return false;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &set, 0, nullptr);
    return true;
  }
  VkResult create_buffer(GpuFailurePoint stage, const char *operation,
                         const VkBufferCreateInfo *info,
                         VkBuffer *out) noexcept {
    if (auto r = allocation_stage(stage, operation); r != VK_SUCCESS)
      return r;
    return tracked_vkCreateBuffer(d_, info, nullptr, out);
  }
  VkResult allocate_buffer_memory(GpuFailurePoint stage,
                                  const VkMemoryAllocateInfo *info,
                                  VkDeviceMemory *out) noexcept {
    if (auto r = allocation_stage(stage, "vkAllocateMemory(buffer)");
        r != VK_SUCCESS)
      return r;
    return tracked_vkAllocateMemory(d_, info, nullptr, out);
  }
  VkResult bind_buffer_memory(GpuFailurePoint stage, VkBuffer buffer,
                              VkDeviceMemory memory) noexcept {
    if (auto r = injected_vk_result(stage, "vkBindBufferMemory");
        r != VK_SUCCESS)
      return r;
    return vkBindBufferMemory(d_, buffer, memory, 0);
  }
  bool copy_buffer_to_image(GpuFailurePoint stage, VkCommandBuffer command,
                            VkBuffer buffer, VkImage image,
                            const VkBufferImageCopy *copy) noexcept {
    if (injected_vk_result(stage, "vkCmdCopyBufferToImage") != VK_SUCCESS)
      return false;
    vkCmdCopyBufferToImage(command, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, copy);
    return true;
  }
  VkResult map_upload(GpuFailurePoint stage, VkDeviceMemory memory,
                      VkDeviceSize size, void **out) noexcept {
    if (auto r = injected_vk_result(stage, "vkMapMemory/upload");
        r != VK_SUCCESS)
      return r;
    return vkMapMemory(d_, memory, 0, size, 0, out);
  }
  std::shared_ptr<VkPipelineOwner>
  color_pipeline(bool curves, const ShaderCompileResult &binary) noexcept {
    std::string identity = (curves ? "rgb-curves:spirv-layout-v1:"
                                   : "primary-wheels:spirv-layout-v1:") +
                           binary.cache_key;
    const auto requested = gpu_failure_point();
    if (requested == GpuFailurePoint::ShaderCompilation ||
        requested == GpuFailurePoint::DescriptorSetLayoutCreation ||
        requested == GpuFailurePoint::PipelineLayoutCreation ||
        requested == GpuFailurePoint::PipelineCreation)
      identity +=
          ":injected-create:" + std::string(gpu_failure_point_name(requested));
    NativePipelineCacheKey key{DIGITOR_RENDERER_VULKAN,
                               reinterpret_cast<std::uintptr_t>(d_),
                               identity,
                               1,
                               GpuPrecisionMode::Float32,
                               DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
    return std::static_pointer_cast<VkPipelineOwner>(
        pipeline_cache_.get_or_create(
            key, [&]() -> NativePipelineCache::Object {
              auto p = std::make_shared<VkPipelineOwner>();
              p->device = d_;
              p->device_live = device_live_;
              VkDescriptorSetLayoutBinding b[4]{
                  {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                  {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                  {2,
                   curves ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                          : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                   1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                  {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
              VkDescriptorSetLayoutCreateInfo di{
                  VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
              di.bindingCount = curves ? 4u : 3u;
              di.pBindings = b;
              if (inject_at(GpuFailurePoint::DescriptorSetLayoutCreation,
                            "vkCreateDescriptorSetLayout") != DIGITOR_RESULT_OK)
                return {};
              if (tracked_vkCreateDescriptorSetLayout(
                      d_, &di, nullptr, &p->descriptors) != VK_SUCCESS)
                return {};
              VkPipelineLayoutCreateInfo li{
                  VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
              li.setLayoutCount = 1;
              li.pSetLayouts = &p->descriptors;
              if (inject_at(GpuFailurePoint::PipelineLayoutCreation,
                            "vkCreatePipelineLayout") != DIGITOR_RESULT_OK)
                return {};
              if (tracked_vkCreatePipelineLayout(d_, &li, nullptr,
                                                 &p->layout) != VK_SUCCESS)
                return {};
              VkShaderModuleCreateInfo si{
                  VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
              si.codeSize = binary.binary.size();
              si.pCode =
                  reinterpret_cast<const uint32_t *>(binary.binary.data());
              if (inject_at(GpuFailurePoint::ShaderCompilation,
                            "vkCreateShaderModule") != DIGITOR_RESULT_OK)
                return {};
              if (tracked_vkCreateShaderModule(d_, &si, nullptr, &p->shader) !=
                  VK_SUCCESS)
                return {};
              VkComputePipelineCreateInfo ci{
                  VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
              ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
              ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
              ci.stage.module = p->shader;
              ci.stage.pName = "main";
              ci.layout = p->layout;
              if (inject_at(GpuFailurePoint::PipelineCreation,
                            "vkCreateComputePipelines") != DIGITOR_RESULT_OK)
                return {};
              if (tracked_vkCreateComputePipelines(d_, VK_NULL_HANDLE, 1, &ci,
                                                   nullptr,
                                                   &p->pipeline) != VK_SUCCESS)
                return {};
              return std::static_pointer_cast<void>(p);
            }));
  }
  uint32_t mem(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t n = 0; n < mp_.memoryTypeCount; n++)
      if ((bits & (1u << n)) &&
          (mp_.memoryTypes[n].propertyFlags & flags) == flags)
        return n;
    return UINT32_MAX;
  }
  static VkFormat fmt(DigitorPixelFormat f) {
    switch (f) {
    case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
      return VK_FORMAT_UNDEFINED;
    }
  }


  struct NodeTexture {
    VkImage image{};
    VkImageView view{};
    VkImageLayout* layout{};
    bool output{};
  };

  static void replace_all(std::string& value, std::string_view from,
                          std::string_view to) {
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
      value.replace(position, from.size(), to);
      position += to.size();
    }
  }

  std::string vulkan_node_hlsl(NativeNodeKernel kernel) const {
    auto source = std::string(native_node_pipeline_contract(
        DIGITOR_RENDERER_D3D12, kernel).source);
    switch (kernel) {
      case NativeNodeKernel::hsl_matte:
        replace_all(source, "Texture2D<float4> Source : register(t0);",
                    "[[vk::binding(0,0)]] Texture2D<float4> Source;");
        replace_all(source, "RWTexture2D<float> Matte : register(u0);",
                    "[[vk::binding(1,0)]] RWTexture2D<float> Matte;");
        break;
      case NativeNodeKernel::power_window_matte:
        replace_all(source, "RWTexture2D<float> Matte : register(u0);",
                    "[[vk::binding(0,0)]] RWTexture2D<float> Matte;");
        break;
      case NativeNodeKernel::matte_multiply:
        replace_all(source, "Texture2D<float> A : register(t0);",
                    "[[vk::binding(0,0)]] Texture2D<float> A;");
        replace_all(source, "Texture2D<float> B : register(t1);",
                    "[[vk::binding(1,0)]] Texture2D<float> B;");
        replace_all(source, "RWTexture2D<float> Output : register(u0);",
                    "[[vk::binding(2,0)]] RWTexture2D<float> Output;");
        break;
      case NativeNodeKernel::masked_composite:
        replace_all(source, "Texture2D<float4> Original : register(t0);",
                    "[[vk::binding(0,0)]] Texture2D<float4> Original;");
        replace_all(source, "Texture2D<float4> Processed : register(t1);",
                    "[[vk::binding(1,0)]] Texture2D<float4> Processed;");
        replace_all(source, "Texture2D<float> Matte : register(t2);",
                    "[[vk::binding(2,0)]] Texture2D<float> Matte;");
        replace_all(source, "RWTexture2D<float4> Output : register(u0);",
                    "[[vk::binding(3,0)]] RWTexture2D<float4> Output;");
        break;
      default:
        break;
    }
    replace_all(source, "cbuffer Params : register(b0)",
                "[[vk::push_constant]] cbuffer Params");
    return source;
  }

  DigitorResult create_node_image(std::uint32_t width, std::uint32_t height,
                                  VkFormat format, VkImage& image,
                                  VkDeviceMemory& memory,
                                  VkImageView& view) noexcept {
    VkImageCreateInfo create_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.extent = {width, height, 1};
    create_info.mipLevels = create_info.arrayLayers = 1;
    create_info.format = format;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (allocation_stage(GpuFailurePoint::OutputResourceCreation,
                         "vkCreateImage(native node)") != VK_SUCCESS ||
        tracked_vkCreateImage(d_, &create_info, nullptr, &image) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(d_, image, &requirements);
    const auto memory_type = mem(requirements.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    if (memory_type == UINT32_MAX ||
        allocation_stage(GpuFailurePoint::OutputMemoryAllocation,
                         "vkAllocateMemory(native node)") != VK_SUCCESS ||
        tracked_vkAllocateMemory(d_, &allocation, nullptr, &memory) !=
            VK_SUCCESS ||
        injected_vk_result(GpuFailurePoint::OutputMemoryBinding,
                           "vkBindImageMemory(native node)") != VK_SUCCESS ||
        vkBindImageMemory(d_, image, memory, 0) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (create_image_view(&view_info, &view) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult dispatch_node_kernel(
      NativeNodeKernel kernel, std::uint32_t width, std::uint32_t height,
      std::span<const NodeTexture> textures, const void* constants,
      std::size_t constant_bytes) noexcept {
    const auto contract = native_node_pipeline_contract(
        DIGITOR_RENDERER_VULKAN, kernel);
    if (!validate_native_node_pipeline_contract(contract) || !constants ||
        constant_bytes != contract.constant_bytes)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = vulkan_node_hlsl(kernel),
        .entry_point = "main",
        .source_name = "native_node_mask.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    const auto compiled_shader = vulkan_shader(request);
    if (!compiled_shader) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    NativeNodeShaderBinary binary;
    binary.format = NativeNodeBinaryFormat::spirv;
    binary.bytes = compiled_shader.binary;
    const auto prepared = prepare_native_node_pipeline(
        DIGITOR_RENDERER_VULKAN, kernel, width, height);
    binary.contract_hash = prepared.contract_hash;

    NativeNodeBackendPipelineHandle pipeline{};
    NativeNodePlatformFactoryContext pipeline_context{};
    pipeline_context.device = reinterpret_cast<std::uintptr_t>(d_);
    std::string diagnostic;
    if (!create_vulkan_native_node_pipeline(pipeline_context, prepared, binary,
                                             pipeline, diagnostic))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    VkDescriptorPool descriptor_pool{};
    VkCommandBuffer command{};
    auto cleanup = [&] {
      if (command) tracked_vkFreeCommandBuffers(d_, pool_, 1, &command);
      if (descriptor_pool)
        tracked_vkDestroyDescriptorPool(d_, descriptor_pool, nullptr);
      destroy_vulkan_native_node_pipeline(pipeline_context, pipeline);
    };
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                   static_cast<std::uint32_t>(textures.size())};
    VkDescriptorPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (create_descriptor_pool(&pool_info, &descriptor_pool) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferAllocateInfo allocation{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = pool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (allocate_command_buffers(&allocation, &command) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(command, &begin) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto transition = [&](NodeTexture texture, VkImageLayout next,
                          VkAccessFlags destination_access) {
      VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      barrier.oldLayout = texture.layout ? *texture.layout
                                         : VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = next;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.srcAccessMask = barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                  ? 0
                                  : VK_ACCESS_SHADER_READ_BIT |
                                        VK_ACCESS_SHADER_WRITE_BIT;
      barrier.dstAccessMask = destination_access;
      barrier.image = texture.image;
      barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(command,
                           barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                               ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                               : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &barrier);
      if (texture.layout) *texture.layout = next;
    };
    for (const auto& texture : textures)
      transition(texture, VK_IMAGE_LAYOUT_GENERAL,
                 texture.output ? VK_ACCESS_SHADER_WRITE_BIT
                                : VK_ACCESS_SHADER_READ_BIT);

    NativeNodeDispatchResources resources;
    resources.kernel = kernel;
    resources.constants.resize(constant_bytes);
    std::memcpy(resources.constants.data(), constants, constant_bytes);
    std::uint32_t texture_index = 0;
    for (std::uint32_t i = 0; i < contract.binding_count; ++i) {
      if (contract.bindings[i].kind == NativeNodeBindingKind::constants)
        continue;
      const auto& texture = textures[texture_index++];
      resources.textures.push_back(
          {contract.bindings[i].binding,
           static_cast<NativeNodeNativeHandle>(texture.view), width, height});
    }
    NativeNodePlatformFactoryContext dispatch_context{};
    dispatch_context.device = reinterpret_cast<std::uintptr_t>(d_);
    dispatch_context.command_context =
        reinterpret_cast<std::uintptr_t>(command);
    dispatch_context.descriptor_context =
        static_cast<NativeNodeNativeHandle>(descriptor_pool);
    if (!record_vulkan_native_node_dispatch(
            dispatch_context, pipeline, prepared.geometry, resources,
            diagnostic)) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    for (const auto& texture : textures)
      transition(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_READ_BIT);
    if (end_command_buffer(command) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    auto result = queue_submit(&submit);
    if (result == VK_SUCCESS) result = queue_wait_idle();
    cleanup();
    return result == VK_SUCCESS ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

public:
  [[nodiscard]] NativeNodeMaskCapabilities
  native_node_mask_capabilities() const noexcept override {
    return {true, true, true, true};
  }

  DigitorResult generate_hsl_matte(
      const GpuSourceResource& source, std::int64_t timestamp,
      const HslQualifierParameters& parameters,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (!source.usable_by(DIGITOR_RENDERER_VULKAN,
                          backend_context_identity()))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto prior = std::static_pointer_cast<VkPreviewOwner>(
        native_owner(*source.frame));
    if (!prior || !prior->output || !prior->output_view)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto owner = std::make_shared<VkMatteOwner>();
    owner->device = d_;
    owner->device_live = device_live_;
    if (create_node_image(source.width, source.height, VK_FORMAT_R32_SFLOAT,
                          owner->image, owner->memory, owner->view) !=
        DIGITOR_RESULT_OK)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    owner->upstream.push_back(prior);
    const auto& values = parameters.values();
    VkHslMatteConstants constants{};
    const auto set_range = [](float (&target)[4], const QualifierRange& range) {
      target[0] = range.low; target[1] = range.high;
      target[2] = range.softness; target[3] = 0.0f;
    };
    set_range(constants.hue, values.hue);
    set_range(constants.saturation, values.saturation);
    set_range(constants.luminance, values.luminance);
    constants.clean_black = values.clean_black;
    constants.clean_white = values.clean_white;
    constants.denoise = values.denoise;
    constants.blur = values.blur;
    constants.invert = values.invert ? 1u : 0u;
    constants.width = source.width;
    constants.height = source.height;
    const NodeTexture textures[]{
        {prior->output, prior->output_view, &prior->output_layout, false},
        {owner->image, owner->view, &owner->layout, true}};
    const auto status = dispatch_node_kernel(
        NativeNodeKernel::hsl_matte, source.width, source.height, textures,
        &constants, sizeof(constants));
    if (status != DIGITOR_RESULT_OK) return status;
    static std::atomic_uint64_t identities{400000};
    output = std::make_shared<GpuMatteResource>(
        DIGITOR_RENDERER_VULKAN, backend_context_identity(),
        GpuMatteMetadata{source.width, source.height, timestamp,
                         GpuMatteFormat::r32_float},
        identities++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    return DIGITOR_RESULT_OK;
  }

  DigitorResult generate_power_window_matte(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp,
      const PowerWindowSettings& settings,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (!width || !height || !d_) return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto owner = std::make_shared<VkMatteOwner>();
    owner->device = d_;
    owner->device_live = device_live_;
    if (create_node_image(width, height, VK_FORMAT_R32_SFLOAT, owner->image,
                          owner->memory, owner->view) != DIGITOR_RESULT_OK)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkWindowConstants constants{
        settings.center_x, settings.center_y, settings.width, settings.height,
        settings.rotation, settings.feather, settings.opacity,
        static_cast<std::uint32_t>(settings.shape), settings.invert ? 1u : 0u,
        width, height, 0u};
    const NodeTexture textures[]{
        {owner->image, owner->view, &owner->layout, true}};
    const auto status = dispatch_node_kernel(
        NativeNodeKernel::power_window_matte, width, height, textures,
        &constants, sizeof(constants));
    if (status != DIGITOR_RESULT_OK) return status;
    static std::atomic_uint64_t identities{500000};
    output = std::make_shared<GpuMatteResource>(
        DIGITOR_RENDERER_VULKAN, backend_context_identity(),
        GpuMatteMetadata{width, height, timestamp, GpuMatteFormat::r32_float},
        identities++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    return DIGITOR_RESULT_OK;
  }

  DigitorResult multiply_mattes(
      std::span<const GpuMatteResourcePtr> inputs, std::int64_t timestamp,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (inputs.empty()) return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (inputs.size() == 1) { output = inputs.front(); return DIGITOR_RESULT_OK; }
    GpuMatteResourcePtr current = inputs.front();
    for (std::size_t index = 1; index < inputs.size(); ++index) {
      const auto& rhs = inputs[index];
      if (!current || !rhs ||
          !current->usable_by(DIGITOR_RENDERER_VULKAN,
                              backend_context_identity()) ||
          !rhs->usable_by(DIGITOR_RENDERER_VULKAN,
                          backend_context_identity()) ||
          current->metadata().width != rhs->metadata().width ||
          current->metadata().height != rhs->metadata().height)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      auto left = std::static_pointer_cast<VkMatteOwner>(
          current->native_owner());
      auto right = std::static_pointer_cast<VkMatteOwner>(rhs->native_owner());
      auto owner = std::make_shared<VkMatteOwner>();
      owner->device = d_;
      owner->device_live = device_live_;
      if (create_node_image(current->metadata().width,
                            current->metadata().height, VK_FORMAT_R32_SFLOAT,
                            owner->image, owner->memory, owner->view) !=
          DIGITOR_RESULT_OK)
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      owner->layout = VK_IMAGE_LAYOUT_UNDEFINED;
      owner->upstream = {left, right};
      VkSizeConstants constants{current->metadata().width,
                                current->metadata().height};
      const NodeTexture textures[]{
          {left->image, left->view, &left->layout, false},
          {right->image, right->view, &right->layout, false},
          {owner->image, owner->view, &owner->layout, true}};
      const auto status = dispatch_node_kernel(
          NativeNodeKernel::matte_multiply, constants.width, constants.height,
          textures, &constants, sizeof(constants));
      if (status != DIGITOR_RESULT_OK) return status;
      static std::atomic_uint64_t identities{600000};
      current = std::make_shared<GpuMatteResource>(
          DIGITOR_RENDERER_VULKAN, backend_context_identity(),
          GpuMatteMetadata{constants.width, constants.height, timestamp,
                           GpuMatteFormat::r32_float},
          identities++, std::static_pointer_cast<void>(owner),
          std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    }
    output = std::move(current);
    return DIGITOR_RESULT_OK;
  }

  DigitorResult composite_with_matte(
      const GpuSourceResource& original, const GpuSourceResource& processed,
      const GpuMatteResourcePtr& matte, std::int64_t timestamp,
      ProcessedGpuFramePtr& output) noexcept override {
    output.reset();
    if (!original.usable_by(DIGITOR_RENDERER_VULKAN,
                            backend_context_identity()) ||
        !processed.usable_by(DIGITOR_RENDERER_VULKAN,
                             backend_context_identity()) ||
        !matte || !matte->usable_by(DIGITOR_RENDERER_VULKAN,
                                    backend_context_identity()) ||
        original.width != processed.width || original.height != processed.height ||
        original.width != matte->metadata().width ||
        original.height != matte->metadata().height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto original_owner = std::static_pointer_cast<VkPreviewOwner>(
        native_owner(*original.frame));
    auto processed_owner = std::static_pointer_cast<VkPreviewOwner>(
        native_owner(*processed.frame));
    auto matte_owner = std::static_pointer_cast<VkMatteOwner>(
        matte->native_owner());
    auto owner = std::make_shared<VkPreviewOwner>();
    owner->device = d_;
    owner->device_live = device_live_;
    if (create_node_image(original.width, original.height,
                          VK_FORMAT_R32G32B32A32_SFLOAT, owner->output,
                          owner->output_memory, owner->output_view) !=
        DIGITOR_RESULT_OK)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->output_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    owner->upstream = std::make_shared<std::vector<std::shared_ptr<void>>>(
        std::initializer_list<std::shared_ptr<void>>{original_owner,
                                                     processed_owner,
                                                     matte_owner});
    VkSizeConstants constants{original.width, original.height};
    const NodeTexture textures[]{
        {original_owner->output, original_owner->output_view,
         &original_owner->output_layout, false},
        {processed_owner->output, processed_owner->output_view,
         &processed_owner->output_layout, false},
        {matte_owner->image, matte_owner->view, &matte_owner->layout, false},
        {owner->output, owner->output_view, &owner->output_layout, true}};
    const auto status = dispatch_node_kernel(
        NativeNodeKernel::masked_composite, original.width, original.height,
        textures, &constants, sizeof(constants));
    if (status != DIGITOR_RESULT_OK) return status;
    static std::atomic_uint64_t identities{700000};
    output = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{original.width, original.height,
                         DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp,
                         original.color_metadata_identity},
        identities++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    bind_frame_context_lifetime(output);
    return DIGITOR_RESULT_OK;
  }

  VulkanBackend(VkInstance in, VkPhysicalDevice ph, VkDevice d, uint32_t family)
      : in_(in), ph_(ph), d_(d), family_(family) {
    vkGetPhysicalDeviceMemoryProperties(ph_, &mp_);
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(ph_, &p);
    i_.backend = DIGITOR_RENDERER_VULKAN;
    copy_bounded(i_.backend_name, "Vulkan");
    copy_bounded(i_.device_name, p.deviceName);
    i_.is_gpu = i_.supports_compute = i_.supports_fp32 = 1;
    vkGetDeviceQueue(d_, family_, 0, &queue_);
  }
  ~VulkanBackend() {
    shutdown();
    retire_context_resources();
    device_live_->store(false, std::memory_order_release);
    if (pool_)
      vkDestroyCommandPool(d_, pool_, nullptr);
    if (d_)
      vkDestroyDevice(d_, nullptr);
    if (in_)
      vkDestroyInstance(in_, nullptr);
  }
  bool initialize(bool) override {
    VkCommandPoolCreateInfo c{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    c.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    c.queueFamilyIndex = family_;
    return vkCreateCommandPool(d_, &c, nullptr, &pool_) == VK_SUCCESS;
  }
  void shutdown() noexcept override {
    if (d_)
      vkDeviceWaitIdle(d_);
    pipeline_cache_.invalidate_device(DIGITOR_RENDERER_VULKAN,
                                      reinterpret_cast<std::uintptr_t>(d_));
  }
  DigitorRendererInfo info() const noexcept override { return i_; }
  NativePipelineCacheCounters
  native_pipeline_cache_counters() const noexcept override {
    return pipeline_cache_.counters();
  }
  std::size_t native_pipeline_cache_size() const noexcept override {
    return pipeline_cache_.size();
  }
  void clear_native_pipeline_cache_for_test() noexcept override {
    pipeline_cache_.invalidate_device(DIGITOR_RENDERER_VULKAN,
                                      reinterpret_cast<std::uintptr_t>(d_));
  }
  NativeResourceCounts native_resource_counts() const noexcept override {
    return vk_live.snapshot();
  }
  DigitorResult execute_process_primary_wheels_gpu(
      std::span<const Color> src, uint32_t width, uint32_t height,
      int64_t timestamp, const PrimaryWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "primary-wheels/cpu-source");
    if (!width || !height || src.size() != size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_primary_wheels_hlsl,
        .entry_point = "main",
        .source_name = "primary_wheels.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner =
        std::shared_ptr<VkPreviewOwner>(new (std::nothrow) VkPreviewOwner{});
    if (!owner)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->device = d_;
    owner->device_live = device_live_;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {width, height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::SourceResourceCreation,
               GpuFailurePoint::SourceMemoryAllocation,
               GpuFailurePoint::SourceMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->source, owner->source_memory) ||
        !image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    auto view = [&](VkImage i, VkImageView &v) {
      VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      c.image = i;
      c.viewType = VK_IMAGE_VIEW_TYPE_2D;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      return create_image_view(&c, &v) == VK_SUCCESS;
    };
    if (!view(owner->source, owner->source_view) ||
        !view(owner->output, owner->output_view))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto params = native_primary_wheels_parameters(
        parameters, uint32_t(src.size()), width, height);
    VkBuffer buffers[2]{};
    VkDeviceMemory memory[2]{};
    VkDeviceSize sizes[]{src.size_bytes(), sizeof(params)};
    auto clean_buffers = [&] {
      for (int n = 0; n < 2; n++) {
        if (buffers[n])
          tracked_vkDestroyBuffer(d_, buffers[n], nullptr);
        if (memory[n])
          tracked_vkFreeMemory(d_, memory[n], nullptr);
      }
    };
    for (int n = 0; n < 2; n++) {
      VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      c.size = sizes[n];
      c.usage = n ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                  : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (create_buffer(n ? GpuFailurePoint::ParameterResourceCreation
                          : GpuFailurePoint::SourceResourceCreation,
                        n ? "vkCreateBuffer(parameter)"
                          : "vkCreateBuffer(source-upload)",
                        &c, &buffers[n]) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, buffers[n], &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      if (mt == UINT32_MAX ||
          allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &a,
                                 &memory[n]) != VK_SUCCESS ||
          bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, buffers[n],
                             memory[n]) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      void *m = nullptr;
      if (map_upload(n ? GpuFailurePoint::ParameterUpload
                       : GpuFailurePoint::SourceUpload,
                     memory[n], sizes[n], &m) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      std::memcpy(m,
                  n ? static_cast<void *>(&params)
                    : static_cast<const void *>(src.data()),
                  sizes[n]);
      vkUnmapMemory(d_, memory[n]);
    }
    auto cached = color_pipeline(false, binary);
    if (!cached) {
      clean_buffers();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto finish = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      clean_buffers();
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 3;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, owner->source_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{buffers[1], 0, sizeof(params)};
    VkWriteDescriptorSet w[3]{};
    for (int n = 0; n < 3; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi;
    update_descriptors(3, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto barrier = [&](VkImage image, VkImageLayout old, VkImageLayout now,
                       VkAccessFlags sa, VkAccessFlags da,
                       VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
      VkImageMemoryBarrier x{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      x.oldLayout = old;
      x.newLayout = now;
      x.srcAccessMask = sa;
      x.dstAccessMask = da;
      x.image = image;
      x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &x);
    };
    barrier(owner->source, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    copy_buffer_to_image(GpuFailurePoint::SourceUploadRecording, cmd,
                         buffers[0], owner->source, &copy);
    barrier(owner->source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier(owner->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (uint32_t(src.size()) + 63) / 64, 1, 1);
    barrier(owner->output, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto result = queue_submit(&sub);
    if (result == VK_SUCCESS)
      result = queue_wait_idle();
    finish();
    if (result != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{100000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp, "linear-rgba"},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.primary_wheels_enabled = true;
    provenance_.primary_wheels_parameter_identity = parameters.identity();
    provenance_.primary_wheels_shader_identity =
        "primary_wheels.hlsl:SPIR-V-v1";
    provenance_.primary_wheels_pipeline_identity =
        "VkPipeline:primary-wheels-v1";
    provenance_.primary_wheels_source_bound =
        provenance_.primary_wheels_destination_bound =
            provenance_.primary_wheels_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_primary_wheels_gpu(
      const GpuSourceResource &s, int64_t timestamp,
      const PrimaryWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "primary-wheels/gpu-source");
    auto prior =
        std::static_pointer_cast<VkPreviewOwner>(native_owner(*s.frame));
    if (!prior || prior->device != d_ || !prior->output ||
        !prior->output_view ||
        prior->output_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_primary_wheels_hlsl,
        .entry_point = "main",
        .source_name = "primary_wheels.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner = std::make_shared<VkPreviewOwner>();
    owner->device = d_;
    owner->device_live = device_live_;
    owner->upstream = prior;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {s.width, s.height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = owner->output;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (create_image_view(&vi, &owner->output_view) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto params = native_primary_wheels_parameters(
        parameters, s.width * s.height, s.width, s.height);
    VkBuffer pb{};
    VkDeviceMemory pm{};
    auto clean_parameter = [&] {
      if (pb)
        tracked_vkDestroyBuffer(d_, pb, nullptr);
      if (pm)
        tracked_vkFreeMemory(d_, pm, nullptr);
    };
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = sizeof(params);
    bc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (create_buffer(GpuFailurePoint::ParameterResourceCreation,
                      "vkCreateBuffer(parameter)", &bc, &pb) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(d_, pb, &mr);
    auto mt = mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (mt == UINT32_MAX ||
        allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &ma,
                               &pm) != VK_SUCCESS ||
        bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, pb, pm) !=
            VK_SUCCESS) {
      clean_parameter();
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    void *m = nullptr;
    if (map_upload(GpuFailurePoint::ParameterUpload, pm, sizeof(params), &m) !=
        VK_SUCCESS) {
      clean_parameter();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    std::memcpy(m, &params, sizeof(params));
    vkUnmapMemory(d_, pm);
    auto cached = color_pipeline(false, binary);
    if (!cached) {
      clean_parameter();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto done = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      if (pb)
        tracked_vkDestroyBuffer(d_, pb, nullptr);
      if (pm)
        tracked_vkFreeMemory(d_, pm, nullptr);
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 3;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, prior->output_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{pb, 0, sizeof(params)};
    VkWriteDescriptorSet w[3]{};
    for (int n = 0; n < 3; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi;
    update_descriptors(3, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkImageMemoryBarrier bars[2]{};
    for (auto &b : bars) {
      b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    bars[0].image = prior->output;
    bars[0].oldLayout = bars[0].newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].image = owner->output;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bars[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, bars);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (s.width * s.height + 63) / 64, 1, 1);
    bars[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &bars[1]);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto vr = queue_submit(&sub);
    if (vr == VK_SUCCESS)
      vr = queue_wait_idle();
    done();
    if (vr != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{200000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{s.width, s.height, s.format, GpuFrameAlpha::straight,
                         timestamp, s.color_metadata_identity},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.primary_wheels_source_bound =
        provenance_.primary_wheels_destination_bound =
            provenance_.primary_wheels_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_validation_readback_primary_wheels(
      const ProcessedGpuFramePtr &frame,
      std::span<Color> out) noexcept override {
    QualificationScope qualification(*this, "validation-readback");
    if (!frame || out.size() != size_t(frame->metadata().width) *
                                    frame->metadata().height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto owner = std::static_pointer_cast<VkPreviewOwner>(native_owner(*frame));
    if (!owner)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    VkDeviceSize bytes = out.size_bytes();
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = bytes;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (create_buffer(GpuFailurePoint::ValidationReadbackResourceCreation,
                      "vkCreateBuffer(validation-readback)", &bc,
                      &buffer) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(d_, buffer, &mr);
    auto mt = mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (mt == UINT32_MAX ||
        allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &ma,
                               &memory) != VK_SUCCESS ||
        bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, buffer,
                           memory) != VK_SUCCESS) {
      tracked_vkDestroyBuffer(d_, buffer, nullptr);
      if (memory)
        tracked_vkFreeMemory(d_, memory, nullptr);
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      tracked_vkDestroyBuffer(d_, buffer, nullptr);
      tracked_vkFreeMemory(d_, memory, nullptr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      tracked_vkDestroyBuffer(d_, buffer, nullptr);
      tracked_vkFreeMemory(d_, memory, nullptr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.image = owner->output;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {frame->metadata().width, frame->metadata().height, 1};
    if (injected_vk_result(GpuFailurePoint::ValidationReadbackCopy,
                           "vkCmdCopyImageToBuffer(validation)") == VK_SUCCESS)
      vkCmdCopyImageToBuffer(cmd, owner->output,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                             &copy);
    std::swap(b.oldLayout, b.newLayout);
    std::swap(b.srcAccessMask, b.dstAccessMask);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &b);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto result = queue_submit(&sub);
    if (result == VK_SUCCESS)
      result = queue_wait_idle();
    void *m = nullptr;
    if (result == VK_SUCCESS &&
        injected_vk_result(GpuFailurePoint::ValidationReadbackMap,
                           "vkMapMemory(validation)") == VK_SUCCESS &&
        vkMapMemory(d_, memory, 0, bytes, 0, &m) == VK_SUCCESS) {
      std::memcpy(out.data(), m, size_t(bytes));
      vkUnmapMemory(d_, memory);
    } else
      result = VK_ERROR_UNKNOWN;
    tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
    tracked_vkDestroyBuffer(d_, buffer, nullptr);
    tracked_vkFreeMemory(d_, memory, nullptr);
    return result == VK_SUCCESS ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult execute_process_log_wheels_gpu(
      std::span<const Color> src, uint32_t width, uint32_t height,
      int64_t timestamp, const LogWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "log-wheels/cpu-source");
    if (!width || !height || src.size() != size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_log_wheels_hlsl,
        .entry_point = "main",
        .source_name = "log_wheels.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner =
        std::shared_ptr<VkPreviewOwner>(new (std::nothrow) VkPreviewOwner{});
    if (!owner)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->device = d_;
    owner->device_live = device_live_;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {width, height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::SourceResourceCreation,
               GpuFailurePoint::SourceMemoryAllocation,
               GpuFailurePoint::SourceMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->source, owner->source_memory) ||
        !image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    auto view = [&](VkImage i, VkImageView &v) {
      VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      c.image = i;
      c.viewType = VK_IMAGE_VIEW_TYPE_2D;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      return create_image_view(&c, &v) == VK_SUCCESS;
    };
    if (!view(owner->source, owner->source_view) ||
        !view(owner->output, owner->output_view))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto params = native_log_wheels_parameters(
        parameters, uint32_t(src.size()), width, height);
    VkBuffer buffers[2]{};
    VkDeviceMemory memory[2]{};
    VkDeviceSize sizes[]{src.size_bytes(), sizeof(params)};
    auto clean_buffers = [&] {
      for (int n = 0; n < 2; n++) {
        if (buffers[n])
          tracked_vkDestroyBuffer(d_, buffers[n], nullptr);
        if (memory[n])
          tracked_vkFreeMemory(d_, memory[n], nullptr);
      }
    };
    for (int n = 0; n < 2; n++) {
      VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      c.size = sizes[n];
      c.usage = n ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                  : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (create_buffer(n ? GpuFailurePoint::ParameterResourceCreation
                          : GpuFailurePoint::SourceResourceCreation,
                        n ? "vkCreateBuffer(parameter)"
                          : "vkCreateBuffer(source-upload)",
                        &c, &buffers[n]) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, buffers[n], &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      if (mt == UINT32_MAX ||
          allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &a,
                                 &memory[n]) != VK_SUCCESS ||
          bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, buffers[n],
                             memory[n]) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      void *m = nullptr;
      if (map_upload(n ? GpuFailurePoint::ParameterUpload
                       : GpuFailurePoint::SourceUpload,
                     memory[n], sizes[n], &m) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      std::memcpy(m,
                  n ? static_cast<void *>(&params)
                    : static_cast<const void *>(src.data()),
                  sizes[n]);
      vkUnmapMemory(d_, memory[n]);
    }
    auto cached = color_pipeline(false, binary);
    if (!cached) {
      clean_buffers();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto finish = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      clean_buffers();
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 3;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, owner->source_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{buffers[1], 0, sizeof(params)};
    VkWriteDescriptorSet w[3]{};
    for (int n = 0; n < 3; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi;
    update_descriptors(3, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto barrier = [&](VkImage image, VkImageLayout old, VkImageLayout now,
                       VkAccessFlags sa, VkAccessFlags da,
                       VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
      VkImageMemoryBarrier x{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      x.oldLayout = old;
      x.newLayout = now;
      x.srcAccessMask = sa;
      x.dstAccessMask = da;
      x.image = image;
      x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &x);
    };
    barrier(owner->source, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    copy_buffer_to_image(GpuFailurePoint::SourceUploadRecording, cmd,
                         buffers[0], owner->source, &copy);
    barrier(owner->source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier(owner->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (uint32_t(src.size()) + 63) / 64, 1, 1);
    barrier(owner->output, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto result = queue_submit(&sub);
    if (result == VK_SUCCESS)
      result = queue_wait_idle();
    finish();
    if (result != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{100000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp, "linear-rgba"},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.log_wheels_enabled = true;
    provenance_.log_wheels_parameter_identity = parameters.identity();
    provenance_.log_wheels_shader_identity =
        "log_wheels.hlsl:SPIR-V-v1";
    provenance_.log_wheels_pipeline_identity =
        "VkPipeline:log-wheels-v1";
    provenance_.log_wheels_source_bound =
        provenance_.log_wheels_destination_bound =
            provenance_.log_wheels_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult execute_process_log_wheels_gpu(
      const GpuSourceResource &s, int64_t timestamp,
      const LogWheelsParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "log-wheels/gpu-source");
    auto prior =
        std::static_pointer_cast<VkPreviewOwner>(native_owner(*s.frame));
    if (!prior || prior->device != d_ || !prior->output ||
        !prior->output_view ||
        prior->output_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_log_wheels_hlsl,
        .entry_point = "main",
        .source_name = "log_wheels.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner = std::make_shared<VkPreviewOwner>();
    owner->device = d_;
    owner->device_live = device_live_;
    owner->upstream = prior;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {s.width, s.height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = owner->output;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (create_image_view(&vi, &owner->output_view) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto params = native_log_wheels_parameters(
        parameters, s.width * s.height, s.width, s.height);
    VkBuffer pb{};
    VkDeviceMemory pm{};
    auto clean_parameter = [&] {
      if (pb)
        tracked_vkDestroyBuffer(d_, pb, nullptr);
      if (pm)
        tracked_vkFreeMemory(d_, pm, nullptr);
    };
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = sizeof(params);
    bc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (create_buffer(GpuFailurePoint::ParameterResourceCreation,
                      "vkCreateBuffer(parameter)", &bc, &pb) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(d_, pb, &mr);
    auto mt = mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (mt == UINT32_MAX ||
        allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &ma,
                               &pm) != VK_SUCCESS ||
        bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, pb, pm) !=
            VK_SUCCESS) {
      clean_parameter();
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    void *m = nullptr;
    if (map_upload(GpuFailurePoint::ParameterUpload, pm, sizeof(params), &m) !=
        VK_SUCCESS) {
      clean_parameter();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    std::memcpy(m, &params, sizeof(params));
    vkUnmapMemory(d_, pm);
    auto cached = color_pipeline(false, binary);
    if (!cached) {
      clean_parameter();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto done = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      if (pb)
        tracked_vkDestroyBuffer(d_, pb, nullptr);
      if (pm)
        tracked_vkFreeMemory(d_, pm, nullptr);
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 3;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, prior->output_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{pb, 0, sizeof(params)};
    VkWriteDescriptorSet w[3]{};
    for (int n = 0; n < 3; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi;
    update_descriptors(3, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkImageMemoryBarrier bars[2]{};
    for (auto &b : bars) {
      b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    bars[0].image = prior->output;
    bars[0].oldLayout = bars[0].newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].image = owner->output;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bars[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, bars);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (s.width * s.height + 63) / 64, 1, 1);
    bars[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &bars[1]);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto vr = queue_submit(&sub);
    if (vr == VK_SUCCESS)
      vr = queue_wait_idle();
    done();
    if (vr != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{200000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{s.width, s.height, s.format, GpuFrameAlpha::straight,
                         timestamp, s.color_metadata_identity},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.log_wheels_source_bound =
        provenance_.log_wheels_destination_bound =
            provenance_.log_wheels_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult execute_process_hsl_qualifier_gpu(
      std::span<const Color> src, uint32_t width, uint32_t height,
      int64_t timestamp, const HslQualifierParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "hsl-qualifier/cpu-source");
    if (!width || !height || src.size() != size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_hsl_qualifier_hlsl,
        .entry_point = "main",
        .source_name = "hsl_qualifier.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner =
        std::shared_ptr<VkPreviewOwner>(new (std::nothrow) VkPreviewOwner{});
    if (!owner)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->device = d_;
    owner->device_live = device_live_;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {width, height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::SourceResourceCreation,
               GpuFailurePoint::SourceMemoryAllocation,
               GpuFailurePoint::SourceMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->source, owner->source_memory) ||
        !image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    auto view = [&](VkImage i, VkImageView &v) {
      VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      c.image = i;
      c.viewType = VK_IMAGE_VIEW_TYPE_2D;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      return create_image_view(&c, &v) == VK_SUCCESS;
    };
    if (!view(owner->source, owner->source_view) ||
        !view(owner->output, owner->output_view))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto params = native_hsl_qualifier_parameters(parameters, width, height);
    VkBuffer buffers[2]{};
    VkDeviceMemory memory[2]{};
    VkDeviceSize sizes[]{src.size_bytes(), sizeof(params)};
    auto clean_buffers = [&] {
      for (int n = 0; n < 2; n++) {
        if (buffers[n])
          tracked_vkDestroyBuffer(d_, buffers[n], nullptr);
        if (memory[n])
          tracked_vkFreeMemory(d_, memory[n], nullptr);
      }
    };
    for (int n = 0; n < 2; n++) {
      VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      c.size = sizes[n];
      c.usage = n ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                  : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (create_buffer(n ? GpuFailurePoint::ParameterResourceCreation
                          : GpuFailurePoint::SourceResourceCreation,
                        n ? "vkCreateBuffer(parameter)"
                          : "vkCreateBuffer(source-upload)",
                        &c, &buffers[n]) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, buffers[n], &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      if (mt == UINT32_MAX ||
          allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &a,
                                 &memory[n]) != VK_SUCCESS ||
          bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, buffers[n],
                             memory[n]) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      void *m = nullptr;
      if (map_upload(n ? GpuFailurePoint::ParameterUpload
                       : GpuFailurePoint::SourceUpload,
                     memory[n], sizes[n], &m) != VK_SUCCESS) {
        clean_buffers();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      std::memcpy(m,
                  n ? static_cast<void *>(&params)
                    : static_cast<const void *>(src.data()),
                  sizes[n]);
      vkUnmapMemory(d_, memory[n]);
    }
    auto cached = color_pipeline(false, binary);
    if (!cached) {
      clean_buffers();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto finish = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      clean_buffers();
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 3;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, owner->source_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{buffers[1], 0, sizeof(params)};
    VkWriteDescriptorSet w[3]{};
    for (int n = 0; n < 3; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi;
    update_descriptors(3, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto barrier = [&](VkImage image, VkImageLayout old, VkImageLayout now,
                       VkAccessFlags sa, VkAccessFlags da,
                       VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
      VkImageMemoryBarrier x{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      x.oldLayout = old;
      x.newLayout = now;
      x.srcAccessMask = sa;
      x.dstAccessMask = da;
      x.image = image;
      x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &x);
    };
    barrier(owner->source, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    copy_buffer_to_image(GpuFailurePoint::SourceUploadRecording, cmd,
                         buffers[0], owner->source, &copy);
    barrier(owner->source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier(owner->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (uint32_t(src.size()) + 63) / 64, 1, 1);
    barrier(owner->output, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto result = queue_submit(&sub);
    if (result == VK_SUCCESS)
      result = queue_wait_idle();
    finish();
    if (result != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{100000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp, "linear-rgba"},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.hsl_qualifier_enabled = true;
    provenance_.hsl_qualifier_parameter_identity = parameters.identity();
    provenance_.hsl_qualifier_shader_identity =
        "hsl_qualifier.hlsl:SPIR-V-v1";
    provenance_.hsl_qualifier_pipeline_identity =
        "VkPipeline:hsl-qualifier-v1";
    provenance_.hsl_qualifier_source_bound =
        provenance_.hsl_qualifier_destination_bound =
            provenance_.hsl_qualifier_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult execute_process_hsl_qualifier_gpu(
      const GpuSourceResource &s, int64_t timestamp,
      const HslQualifierParameters &parameters,
      ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "hsl-qualifier/gpu-source");
    auto prior =
        std::static_pointer_cast<VkPreviewOwner>(native_owner(*s.frame));
    if (!prior || prior->device != d_ || !prior->output ||
        !prior->output_view ||
        prior->output_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_hsl_qualifier_hlsl,
        .entry_point = "main",
        .source_name = "hsl_qualifier.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner = std::make_shared<VkPreviewOwner>();
    owner->device = d_;
    owner->device_live = device_live_;
    owner->upstream = prior;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {s.width, s.height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = owner->output;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (create_image_view(&vi, &owner->output_view) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto params = native_hsl_qualifier_parameters(
        parameters, s.width, s.height);
    VkBuffer pb{};
    VkDeviceMemory pm{};
    auto clean_parameter = [&] {
      if (pb)
        tracked_vkDestroyBuffer(d_, pb, nullptr);
      if (pm)
        tracked_vkFreeMemory(d_, pm, nullptr);
    };
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = sizeof(params);
    bc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (create_buffer(GpuFailurePoint::ParameterResourceCreation,
                      "vkCreateBuffer(parameter)", &bc, &pb) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(d_, pb, &mr);
    auto mt = mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (mt == UINT32_MAX ||
        allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &ma,
                               &pm) != VK_SUCCESS ||
        bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, pb, pm) !=
            VK_SUCCESS) {
      clean_parameter();
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    void *m = nullptr;
    if (map_upload(GpuFailurePoint::ParameterUpload, pm, sizeof(params), &m) !=
        VK_SUCCESS) {
      clean_parameter();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    std::memcpy(m, &params, sizeof(params));
    vkUnmapMemory(d_, pm);
    auto cached = color_pipeline(false, binary);
    if (!cached) {
      clean_parameter();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto done = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      if (pb)
        tracked_vkDestroyBuffer(d_, pb, nullptr);
      if (pm)
        tracked_vkFreeMemory(d_, pm, nullptr);
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 3;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, prior->output_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{pb, 0, sizeof(params)};
    VkWriteDescriptorSet w[3]{};
    for (int n = 0; n < 3; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &bi;
    update_descriptors(3, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      done();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkImageMemoryBarrier bars[2]{};
    for (auto &b : bars) {
      b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    bars[0].image = prior->output;
    bars[0].oldLayout = bars[0].newLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].image = owner->output;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bars[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, bars);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (s.width * s.height + 63) / 64, 1, 1);
    bars[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    bars[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &bars[1]);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto vr = queue_submit(&sub);
    if (vr == VK_SUCCESS)
      vr = queue_wait_idle();
    done();
    if (vr != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{200000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{s.width, s.height, s.format, GpuFrameAlpha::straight,
                         timestamp, s.color_metadata_identity},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.hsl_qualifier_source_bound =
        provenance_.hsl_qualifier_destination_bound =
            provenance_.hsl_qualifier_parameters_bound =
                provenance_.command_recorded =
                    provenance_.dispatch_or_draw_issued =
                        provenance_.queue_submission_issued =
                            provenance_.synchronization_waited =
                                provenance_.output_written = true;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult execute_validation_readback_hsl_qualifier(const ProcessedGpuFramePtr &frame, std::span<float> out) noexcept override {
    if (!frame || out.size()!=std::size_t(frame->metadata().width)*frame->metadata().height) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::vector<Color> rgba(out.size()); auto r=execute_validation_readback_log_wheels(frame,rgba); if(r!=DIGITOR_RESULT_OK)return r; for(std::size_t i=0;i<out.size();++i)out[i]=rgba[i].r; return DIGITOR_RESULT_OK;
  }

  DigitorResult execute_validation_readback_log_wheels(
      const ProcessedGpuFramePtr &frame,
      std::span<Color> out) noexcept override {
    QualificationScope qualification(*this, "validation-readback");
    if (!frame || out.size() != size_t(frame->metadata().width) *
                                    frame->metadata().height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto owner = std::static_pointer_cast<VkPreviewOwner>(native_owner(*frame));
    if (!owner)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    VkDeviceSize bytes = out.size_bytes();
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = bytes;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (create_buffer(GpuFailurePoint::ValidationReadbackResourceCreation,
                      "vkCreateBuffer(validation-readback)", &bc,
                      &buffer) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(d_, buffer, &mr);
    auto mt = mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (mt == UINT32_MAX ||
        allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &ma,
                               &memory) != VK_SUCCESS ||
        bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, buffer,
                           memory) != VK_SUCCESS) {
      tracked_vkDestroyBuffer(d_, buffer, nullptr);
      if (memory)
        tracked_vkFreeMemory(d_, memory, nullptr);
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      tracked_vkDestroyBuffer(d_, buffer, nullptr);
      tracked_vkFreeMemory(d_, memory, nullptr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      tracked_vkDestroyBuffer(d_, buffer, nullptr);
      tracked_vkFreeMemory(d_, memory, nullptr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.image = owner->output;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {frame->metadata().width, frame->metadata().height, 1};
    if (injected_vk_result(GpuFailurePoint::ValidationReadbackCopy,
                           "vkCmdCopyImageToBuffer(validation)") == VK_SUCCESS)
      vkCmdCopyImageToBuffer(cmd, owner->output,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                             &copy);
    std::swap(b.oldLayout, b.newLayout);
    std::swap(b.srcAccessMask, b.dstAccessMask);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &b);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto result = queue_submit(&sub);
    if (result == VK_SUCCESS)
      result = queue_wait_idle();
    void *m = nullptr;
    if (result == VK_SUCCESS &&
        injected_vk_result(GpuFailurePoint::ValidationReadbackMap,
                           "vkMapMemory(validation)") == VK_SUCCESS &&
        vkMapMemory(d_, memory, 0, bytes, 0, &m) == VK_SUCCESS) {
      std::memcpy(out.data(), m, size_t(bytes));
      vkUnmapMemory(d_, memory);
    } else
      result = VK_ERROR_UNKNOWN;
    tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
    tracked_vkDestroyBuffer(d_, buffer, nullptr);
    tracked_vkFreeMemory(d_, memory, nullptr);
    return result == VK_SUCCESS ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  DigitorResult execute_process_curves_gpu(std::span<const Color> src, uint32_t width,
                             uint32_t height, int64_t timestamp,
                             const CompiledRgbCurves &curves,
                             ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "rgb-curves/cpu-source");
    if (!width || !height || src.size() != size_t(width) * height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{
        .source = digitor_rgb_curves_hlsl,
        .entry_point = "main",
        .source_name = "rgb_curves.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner =
        std::shared_ptr<VkPreviewOwner>(new (std::nothrow) VkPreviewOwner{});
    if (!owner)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->device = d_;
    owner->device_live = device_live_;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {width, height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::SourceResourceCreation,
               GpuFailurePoint::SourceMemoryAllocation,
               GpuFailurePoint::SourceMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->source, owner->source_memory) ||
        !image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    auto view = [&](VkImage i, VkImageView &v) {
      VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      c.image = i;
      c.viewType = VK_IMAGE_VIEW_TYPE_2D;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      return create_image_view(&c, &v) == VK_SUCCESS;
    };
    if (!view(owner->source, owner->source_view) ||
        !view(owner->output, owner->output_view))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto lut = native_rgb_curves_lut(curves);
    auto params = native_rgb_curves_parameters(curves, uint32_t(src.size()));
    params.padding[0] = width;
    params.padding[1] = height;
    VkBuffer b[3]{};
    VkDeviceMemory bm[3]{};
    VkDeviceSize sizes[]{src.size_bytes(), lut.size() * sizeof(float),
                         sizeof(params)};
    auto cleanup = [&] {
      for (int n = 0; n < 3; n++) {
        if (b[n])
          tracked_vkDestroyBuffer(d_, b[n], nullptr);
        if (bm[n])
          tracked_vkFreeMemory(d_, bm[n], nullptr);
      }
    };
    for (int n = 0; n < 3; n++) {
      VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      c.size = sizes[n];
      c.usage = n ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                  : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      if (n == 2)
        c.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (create_buffer(n == 0   ? GpuFailurePoint::SourceResourceCreation
                        : n == 1 ? GpuFailurePoint::LutResourceCreation
                                 : GpuFailurePoint::ParameterResourceCreation,
                        n == 0   ? "vkCreateBuffer(source-upload)"
                        : n == 1 ? "vkCreateBuffer(LUT)"
                                 : "vkCreateBuffer(parameter)",
                        &c, &b[n]) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, b[n], &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      if (mt == UINT32_MAX ||
          allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &a,
                                 &bm[n]) != VK_SUCCESS ||
          bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, b[n],
                             bm[n]) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
    }
    void *m = nullptr;
    for (int n = 0; n < 3; n++) {
      const auto upload_stage = n == 0   ? GpuFailurePoint::SourceUpload
                                : n == 1 ? GpuFailurePoint::LutUpload
                                         : GpuFailurePoint::ParameterUpload;
      if (map_upload(upload_stage, bm[n], sizes[n], &m) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      std::memcpy(m,
                  n == 0   ? static_cast<const void *>(src.data())
                  : n == 1 ? static_cast<const void *>(lut.data())
                           : static_cast<const void *>(&params),
                  sizes[n]);
      vkUnmapMemory(d_, bm[n]);
    }
    auto cached = color_pipeline(true, binary);
    if (!cached) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto finish = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      cleanup();
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 4;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, owner->source_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi[2]{{b[1], 0, sizes[1]}, {b[2], 0, sizes[2]}};
    VkWriteDescriptorSet w[4]{};
    for (int n = 0; n < 4; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[2].pBufferInfo = &bi[0];
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[3].pBufferInfo = &bi[1];
    update_descriptors(4, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto barrier = [&](VkImage image, VkImageLayout old, VkImageLayout now,
                       VkAccessFlags srca, VkAccessFlags dsta,
                       VkPipelineStageFlags srcs, VkPipelineStageFlags dsts) {
      VkImageMemoryBarrier x{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      x.oldLayout = old;
      x.newLayout = now;
      x.srcAccessMask = srca;
      x.dstAccessMask = dsta;
      x.image = image;
      x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, srcs, dsts, 0, 0, nullptr, 0, nullptr, 1, &x);
    };
    barrier(owner->source, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    copy_buffer_to_image(GpuFailurePoint::SourceUploadRecording, cmd, b[0],
                         owner->source, &copy);
    barrier(owner->source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier(owner->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (uint32_t(src.size()) + 63) / 64, 1, 1);
    barrier(owner->output, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto vr = queue_submit(&sub);
    if (vr == VK_SUCCESS)
      vr = queue_wait_idle();
    finish();
    if (vr != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{1};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{width, height, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp, "linear-rgba"},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.curve_source_bound = provenance_.curve_destination_bound =
        provenance_.curve_lut_bound = provenance_.curve_parameters_bound =
            provenance_.command_recorded = provenance_.dispatch_or_draw_issued =
                provenance_.queue_submission_issued =
                    provenance_.synchronization_waited =
                        provenance_.output_written = true;
    provenance_.readback_performed = false;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult
  execute_process_curves_gpu(const GpuSourceResource &s, int64_t timestamp,
                             const CompiledRgbCurves &curves,
                             ProcessedGpuFramePtr &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "rgb-curves/gpu-source");
    auto prior =
        std::static_pointer_cast<VkPreviewOwner>(native_owner(*s.frame));
    if (!prior || prior->device != d_ || !prior->output ||
        !prior->output_view ||
        prior->output_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto width = s.width, height = s.height;
    ShaderCompileRequest request{
        .source = digitor_rgb_curves_hlsl,
        .entry_point = "main",
        .source_name = "rgb_curves.hlsl",
        .target_profile = "cs_6_0",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {{"DIGITOR_VULKAN", "1"}, {"DIGITOR_TEXTURE_OUTPUT", "1"}},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    auto binary = vulkan_shader(request);
    if (!binary)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner =
        std::shared_ptr<VkPreviewOwner>(new (std::nothrow) VkPreviewOwner{});
    if (!owner)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->device = d_;
    owner->device_live = device_live_;
    owner->upstream = prior;
    auto image = [&](GpuFailurePoint resource_stage,
                     GpuFailurePoint memory_stage,
                     GpuFailurePoint binding_stage, VkImageUsageFlags usage,
                     VkImage &i, VkDeviceMemory &m) {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.extent = {width, height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      c.usage = usage;
      if (allocation_stage(resource_stage, "vkCreateImage") != VK_SUCCESS ||
          tracked_vkCreateImage(d_, &c, nullptr, &i) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, i, &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      return mt != UINT32_MAX &&
             allocation_stage(memory_stage, "vkAllocateMemory(image)") ==
                 VK_SUCCESS &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             injected_vk_result(binding_stage, "vkBindImageMemory") ==
                 VK_SUCCESS &&
             vkBindImageMemory(d_, i, m, 0) == VK_SUCCESS;
    };
    if (!image(GpuFailurePoint::OutputResourceCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->output, owner->output_memory) ||
        !image(GpuFailurePoint::PreviewDestinationCreation,
               GpuFailurePoint::OutputMemoryAllocation,
               GpuFailurePoint::OutputMemoryBinding,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               owner->preview, owner->preview_memory))
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    auto view = [&](VkImage i, VkImageView &v) {
      VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      c.image = i;
      c.viewType = VK_IMAGE_VIEW_TYPE_2D;
      c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      c.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      return create_image_view(&c, &v) == VK_SUCCESS;
    };
    if (!view(owner->output, owner->output_view))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto lut = native_rgb_curves_lut(curves);
    auto params = native_rgb_curves_parameters(curves, width * height);
    params.padding[0] = width;
    params.padding[1] = height;
    VkBuffer b[3]{};
    VkDeviceMemory bm[3]{};
    VkDeviceSize sizes[]{0, lut.size() * sizeof(float), sizeof(params)};
    auto cleanup = [&] {
      for (int n = 0; n < 3; n++) {
        if (b[n])
          tracked_vkDestroyBuffer(d_, b[n], nullptr);
        if (bm[n])
          tracked_vkFreeMemory(d_, bm[n], nullptr);
      }
    };
    for (int n = 1; n < 3; n++) {
      VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      c.size = sizes[n];
      c.usage = n ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                  : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      if (n == 2)
        c.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (create_buffer(n == 0   ? GpuFailurePoint::SourceResourceCreation
                        : n == 1 ? GpuFailurePoint::LutResourceCreation
                                 : GpuFailurePoint::ParameterResourceCreation,
                        n == 0   ? "vkCreateBuffer(source-upload)"
                        : n == 1 ? "vkCreateBuffer(LUT)"
                                 : "vkCreateBuffer(parameter)",
                        &c, &b[n]) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, b[n], &r);
      auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = mt;
      if (mt == UINT32_MAX ||
          allocate_buffer_memory(GpuFailurePoint::BufferMemoryAllocation, &a,
                                 &bm[n]) != VK_SUCCESS ||
          bind_buffer_memory(GpuFailurePoint::BufferMemoryBinding, b[n],
                             bm[n]) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }
    }
    void *m = nullptr;
    for (int n = 1; n < 3; n++) {
      const auto upload_stage = n == 1 ? GpuFailurePoint::LutUpload
                                       : GpuFailurePoint::ParameterUpload;
      if (map_upload(upload_stage, bm[n], sizes[n], &m) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      std::memcpy(m,
                  n == 1 ? static_cast<const void *>(lut.data())
                         : static_cast<const void *>(&params),
                  sizes[n]);
      vkUnmapMemory(d_, bm[n]);
    }
    auto cached = color_pipeline(true, binary);
    if (!cached) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetLayout sl = cached->descriptors;
    VkPipelineLayout pl = cached->layout;
    VkPipeline pipe = cached->pipeline;
    VkDescriptorPool dp{};
    VkCommandBuffer cmd{};
    auto finish = [&] {
      if (cmd)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      if (dp)
        tracked_vkDestroyDescriptorPool(d_, dp, nullptr);
      cleanup();
    };
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    di.maxSets = 1;
    di.poolSizeCount = 4;
    di.pPoolSizes = ps;
    if (create_descriptor_pool(&di, &dp) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo ai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = dp;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&ai, &set) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorImageInfo ii[2]{
        {VK_NULL_HANDLE, prior->output_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi[2]{{b[1], 0, sizes[1]}, {b[2], 0, sizes[2]}};
    VkWriteDescriptorSet w[4]{};
    for (int n = 0; n < 4; n++) {
      w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[n].dstSet = set;
      w[n].dstBinding = n;
      w[n].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w[0].pImageInfo = &ii[0];
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &ii[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[2].pBufferInfo = &bi[0];
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[3].pBufferInfo = &bi[1];
    update_descriptors(4, w);
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &cb) != VK_SUCCESS) {
      finish();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto barrier = [&](VkImage image, VkImageLayout old, VkImageLayout now,
                       VkAccessFlags srca, VkAccessFlags dsta,
                       VkPipelineStageFlags srcs, VkPipelineStageFlags dsts) {
      VkImageMemoryBarrier x{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      x.oldLayout = old;
      x.newLayout = now;
      x.srcAccessMask = srca;
      x.dstAccessMask = dsta;
      x.image = image;
      x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, srcs, dsts, 0, 0, nullptr, 0, nullptr, 1, &x);
    };
    barrier(prior->output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier(owner->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    bind_compute_resources(cmd, pipe, pl, set);
    dispatch(cmd, (width * height + 63) / 64, 1, 1);
    barrier(owner->output, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    end_command_buffer(cmd);
    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    auto vr = queue_submit(&sub);
    if (vr == VK_SUCCESS)
      vr = queue_wait_idle();
    finish();
    if (vr != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{300000};
    if (injected_vk_result(GpuFailurePoint::ProcessedFrameCreation,
                           "ProcessedGpuFrame construction") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{width, height, s.format, GpuFrameAlpha::straight,
                         timestamp, s.color_metadata_identity},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    provenance_.curve_source_bound = provenance_.curve_destination_bound =
        provenance_.curve_lut_bound = provenance_.curve_parameters_bound =
            provenance_.command_recorded = provenance_.dispatch_or_draw_issued =
                provenance_.queue_submission_issued =
                    provenance_.synchronization_waited =
                        provenance_.output_written = true;
    provenance_.readback_performed = false;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_create_preview_consumer(
      const ProcessedGpuFramePtr &frame,
      std::shared_ptr<PreviewConsumerDestination> &out) noexcept override {
    out.reset();
    QualificationScope qualification(*this, "preview-consumer/create");
    if (!frame || !d_ || !device_live_->load())
      return DIGITOR_RESULT_NOT_INITIALIZED;
    const auto &m = frame->metadata();
    if (injected_vk_result(GpuFailurePoint::PreviewAcquisition,
                           "preview consumer owner acquisition") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner = std::make_shared<VkConsumerOwner>();
    owner->counted = true;
    ++vk_live.consumers;
    owner->device = d_;
    owner->device_live = device_live_;
    VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    c.imageType = VK_IMAGE_TYPE_2D;
    c.extent = {m.width, m.height, 1};
    c.mipLevels = c.arrayLayers = 1;
    c.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    c.tiling = VK_IMAGE_TILING_OPTIMAL;
    c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    c.samples = VK_SAMPLE_COUNT_1_BIT;
    c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    c.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (allocation_stage(GpuFailurePoint::PreviewDestinationCreation,
                         "vkCreateImage(consumer)") != VK_SUCCESS ||
        tracked_vkCreateImage(d_, &c, nullptr, &owner->image) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkMemoryRequirements r{};
    vkGetImageMemoryRequirements(d_, owner->image, &r);
    auto mt = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = r.size;
    a.memoryTypeIndex = mt;
    if (mt == UINT32_MAX ||
        allocation_stage(GpuFailurePoint::OutputMemoryAllocation,
                         "vkAllocateMemory(consumer)") != VK_SUCCESS ||
        tracked_vkAllocateMemory(d_, &a, nullptr, &owner->memory) !=
            VK_SUCCESS ||
        injected_vk_result(GpuFailurePoint::OutputMemoryBinding,
                           "vkBindImageMemory(consumer)") != VK_SUCCESS ||
        vkBindImageMemory(d_, owner->image, owner->memory, 0) != VK_SUCCESS)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = owner->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (create_image_view(&vi, &owner->view) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto live = device_live_;
    auto consumer_live = std::make_shared<std::atomic_bool>(true);
    static std::atomic_uint64_t tokens{1};
    out = std::make_shared<PreviewConsumerDestination>(
        PreviewConsumerMetadata{DIGITOR_RENDERER_VULKAN, this, m.width,
                                m.height, m.format, GpuPrecisionMode::Float32},
        tokens++, std::static_pointer_cast<void>(owner), consumer_live,
        [this, live](const ProcessedGpuFramePtr &f,
                     const std::shared_ptr<void> &d) {
          QualificationScope qualification(*this, "preview-consumer/submit");
          if (!live->load())
            return DIGITOR_RESULT_NOT_INITIALIZED;
          auto source =
              std::static_pointer_cast<VkPreviewOwner>(native_owner(*f));
          auto destination = std::static_pointer_cast<VkConsumerOwner>(d);
          if (!source || !destination || source->device != d_ ||
              destination->device != d_)
            return DIGITOR_RESULT_INVALID_ARGUMENT;
          VkCommandBuffer cmd{};
          VkCommandBufferAllocateInfo ca{
              VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
          ca.commandPool = pool_;
          ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
          ca.commandBufferCount = 1;
          if (allocate_command_buffers(&ca, &cmd) != VK_SUCCESS)
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          VkCommandBufferBeginInfo bi{
              VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
          if (begin_command_buffer(cmd, &bi) != VK_SUCCESS) {
            tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }
          VkImageMemoryBarrier bars[2]{};
          for (auto &b : bars) {
            b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
          }
          bars[0].image = source->output;
          bars[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          bars[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
          bars[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
          bars[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
          bars[1].image = destination->image;
          bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
          bars[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          bars[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
          vkCmdPipelineBarrier(cmd,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                               nullptr, 2, bars);
          VkImageCopy copy{};
          copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
          copy.dstSubresource = copy.srcSubresource;
          copy.extent = {f->metadata().width, f->metadata().height, 1};
          if (injected_vk_result(GpuFailurePoint::PreviewPresentation,
                                 "vkCmdCopyImage(preview presentation)") !=
                  VK_SUCCESS ||
              injected_vk_result(GpuFailurePoint::ConsumerCopySubmission,
                                 "vkCmdCopyImage(consumer)") != VK_SUCCESS) {
            tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }
          vkCmdCopyImage(cmd, source->output,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         destination->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
          bars[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
          bars[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          bars[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
          bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          bars[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          bars[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          bars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
          bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr, 2, bars);
          auto result = end_command_buffer(cmd);
          if (result == VK_SUCCESS) {
            VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            sub.commandBufferCount = 1;
            sub.pCommandBuffers = &cmd;
            result = queue_submit(&sub);
            if (result == VK_SUCCESS)
              result = queue_wait_idle();
          }
          tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
          return result == VK_SUCCESS ? DIGITOR_RESULT_OK
                                      : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        });
    return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_present_gpu_frame(
      const ProcessedGpuFramePtr &frame) noexcept override {
    QualificationScope qualification(*this, "preview-direct/submit");
    if (!frame)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (injected_vk_result(GpuFailurePoint::PreviewAcquisition,
                           "ProcessedGpuFrame::acquire") != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if (frame->acquire(this, DIGITOR_RENDERER_VULKAN) != DIGITOR_RESULT_OK)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto o = std::static_pointer_cast<VkPreviewOwner>(native_owner(*frame));
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo a{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    a.commandPool = pool_;
    a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    a.commandBufferCount = 1;
    if (!o || allocate_command_buffers(&a, &cmd) != VK_SUCCESS) {
      (void)frame->release(this);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (begin_command_buffer(cmd, &b) != VK_SUCCESS) {
      tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      (void)frame->release(this);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkImageMemoryBarrier bars[2]{};
    for (auto &x : bars) {
      x = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    bars[0].image = o->output;
    bars[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bars[1].image = o->preview;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bars[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, bars);
    auto m = frame->metadata();
    VkImageCopy c{};
    c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.dstSubresource = c.srcSubresource;
    c.extent = {m.width, m.height, 1};
    if (injected_vk_result(GpuFailurePoint::PreviewPresentation,
                           "vkCmdCopyImage(direct preview)") != VK_SUCCESS) {
      tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
      (void)frame->release(this);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    vkCmdCopyImage(cmd, o->output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   o->preview, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    bars[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 2, bars);
    end_command_buffer(cmd);
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.commandBufferCount = 1;
    s.pCommandBuffers = &cmd;
    auto v = queue_submit(&s);
    if (v == VK_SUCCESS)
      v = queue_wait_idle();
    tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
    (void)frame->release(this);
    return v == VK_SUCCESS ? DIGITOR_RESULT_OK
                           : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult
  render_rgba8(uint32_t width, uint32_t height, std::span<const uint8_t> source,
               std::vector<uint8_t> &destination) noexcept override {
    if (!width || !height ||
        (!source.empty() && source.size() != size_t(width) * height * 4))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
    VkBuffer staging{}, readback{};
    VkDeviceMemory sm{}, rm{};
    VkImage image{};
    VkDeviceMemory im{};
    auto buffer = [&](VkBuffer &b, VkDeviceMemory &m) {
      VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      c.size = bytes;
      c.usage =
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (tracked_vkCreateBuffer(d_, &c, nullptr, &b) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, b, &r);
      auto n = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = n;
      return n != UINT32_MAX &&
             tracked_vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
             vkBindBufferMemory(d_, b, m, 0) == VK_SUCCESS;
    };
    if (!buffer(staging, sm) || !buffer(readback, rm))
      goto fail;
    {
      void *p = nullptr;
      if (vkMapMemory(d_, sm, 0, bytes, 0, &p) != VK_SUCCESS)
        goto fail;
      if (source.empty()) {
        auto *q = static_cast<uint8_t *>(p);
        for (size_t n = 0; n < size_t(width) * height; ++n) {
          q[n * 4] = q[n * 4 + 1] = q[n * 4 + 2] = 0;
          q[n * 4 + 3] = 255;
        }
      } else
        std::memcpy(p, source.data(), size_t(bytes));
      vkUnmapMemory(d_, sm);
    }
    {
      VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      c.imageType = VK_IMAGE_TYPE_2D;
      c.format = VK_FORMAT_R8G8B8A8_UNORM;
      c.extent = {width, height, 1};
      c.mipLevels = c.arrayLayers = 1;
      c.samples = VK_SAMPLE_COUNT_1_BIT;
      c.tiling = VK_IMAGE_TILING_OPTIMAL;
      c.usage =
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (tracked_vkCreateImage(d_, &c, nullptr, &image) != VK_SUCCESS)
        goto fail;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, image, &r);
      auto n = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = n;
      if (n == UINT32_MAX ||
          tracked_vkAllocateMemory(d_, &a, nullptr, &im) != VK_SUCCESS ||
          vkBindImageMemory(d_, image, im, 0) != VK_SUCCESS)
        goto fail;
    }
    {
      VkCommandBufferAllocateInfo a{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      a.commandPool = pool_;
      a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      a.commandBufferCount = 1;
      VkCommandBuffer cb{};
      if (allocate_command_buffers(&a, &cb) != VK_SUCCESS)
        goto fail;
      VkCommandBufferBeginInfo begin{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      if (begin_command_buffer(cb, &begin) != VK_SUCCESS)
        goto fail;
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.srcAccessMask = 0;
      b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &b);
      VkBufferImageCopy region{};
      region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      region.imageExtent = {width, height, 1};
      if (source.empty()) {
        VkClearColorValue clear{};
        clear.float32[3] = 1.0f;
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &range);
      } else {
        copy_buffer_to_image(GpuFailurePoint::SourceUploadRecording, cb,
                             staging, image, &region);
      }
      b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &b);
      vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             readback, 1, &region);
      if (end_command_buffer(cb) != VK_SUCCESS)
        goto fail;
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cb;
      if (queue_submit(&submit) != VK_SUCCESS ||
          queue_wait_idle() != VK_SUCCESS)
        goto fail;
      tracked_vkFreeCommandBuffers(d_, pool_, 1, &cb);
    }
    try {
      destination.resize(size_t(bytes));
    } catch (...) {
      goto fail;
    }
    {
      void *p = nullptr;
      if (vkMapMemory(d_, rm, 0, bytes, 0, &p) != VK_SUCCESS)
        goto fail;
      std::memcpy(destination.data(), p, size_t(bytes));
      vkUnmapMemory(d_, rm);
    }
    tracked_vkDestroyImage(d_, image, nullptr);
    tracked_vkFreeMemory(d_, im, nullptr);
    tracked_vkDestroyBuffer(d_, readback, nullptr);
    tracked_vkFreeMemory(d_, rm, nullptr);
    tracked_vkDestroyBuffer(d_, staging, nullptr);
    tracked_vkFreeMemory(d_, sm, nullptr);
    return DIGITOR_RESULT_OK;
  fail:
    if (image)
      tracked_vkDestroyImage(d_, image, nullptr);
    if (im)
      tracked_vkFreeMemory(d_, im, nullptr);
    if (readback)
      tracked_vkDestroyBuffer(d_, readback, nullptr);
    if (rm)
      tracked_vkFreeMemory(d_, rm, nullptr);
    if (staging)
      tracked_vkDestroyBuffer(d_, staging, nullptr);
    if (sm)
      tracked_vkFreeMemory(d_, sm, nullptr);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult grade_rgba32f(std::span<const Color> src, std::span<Color> out,
                              const ColorGrade &p) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_VULKAN, true, i_.device_name,
                           shader_compiler_.identity().c_str(),
                           "color_pipeline.hlsl:main",
                           "VkComputePipeline:grade-v1");
    if (src.size() != out.size())
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (src.empty())
      return DIGITOR_RESULT_OK;
    std::uint32_t pixel_count = 0;
    if (!checked_size_to_uint32(src.size(), pixel_count))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    VkBuffer bufs[2]{};
    VkDeviceMemory memory[2]{};
    auto cleanup = [&] {
      for (int k = 0; k < 2; k++) {
        if (bufs[k])
          tracked_vkDestroyBuffer(d_, bufs[k], nullptr);
        if (memory[k])
          tracked_vkFreeMemory(d_, memory[k], nullptr);
      }
    };
    const VkDeviceSize bytes = src.size_bytes();
    for (int k = 0; k < 2; k++) {
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bi.size = bytes;
      bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (tracked_vkCreateBuffer(d_, &bi, nullptr, &bufs[k]) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      VkMemoryRequirements req{};
      vkGetBufferMemoryRequirements(d_, bufs[k], &req);
      uint32_t mt =
          mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      if (mt == UINT32_MAX) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = mt;
      if (tracked_vkAllocateMemory(d_, &ai, nullptr, &memory[k]) !=
              VK_SUCCESS ||
          vkBindBufferMemory(d_, bufs[k], memory[k], 0) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
    void *m = nullptr;
    vkMapMemory(d_, memory[0], 0, bytes, 0, &m);
    std::memcpy(m, src.data(), bytes);
    vkUnmapMemory(d_, memory[0]);
    provenance_.source_upload_performed = true;
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t k = 0; k < 2; k++) {
      bindings[k].binding = k;
      bindings[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[k].descriptorCount = 1;
      bindings[k].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 2;
    li.pBindings = bindings;
    VkDescriptorSetLayout layout{};
    if (tracked_vkCreateDescriptorSetLayout(d_, &li, nullptr, &layout) !=
        VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 48};
    VkPipelineLayoutCreateInfo pli{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &layout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &range;
    VkPipelineLayout pipelineLayout{};
    tracked_vkCreatePipelineLayout(d_, &pli, nullptr, &pipelineLayout);
    ShaderCompileRequest grade_request{.source = digitor_color_pipeline_hlsl,
                                       .entry_point = "main",
                                       .source_name = "color_pipeline.hlsl",
                                       .target_profile = "cs_6_0",
                                       .stage = ShaderStage::compute,
                                       .backend = ShaderBackend::vulkan,
                                       .macros = {{"DIGITOR_VULKAN", "1"}},
                                       .include_roots = {},
                                       .specialization_constants = {},
                                       .optimization =
                                           ShaderOptimization::performance,
                                       .debug_info = false};
    const auto grade_binary =
        vulkan_shader(grade_request);
    if (!grade_binary) {
      cleanup();
      tracked_vkDestroyPipelineLayout(d_, pipelineLayout, nullptr);
      tracked_vkDestroyDescriptorSetLayout(d_, layout, nullptr);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = grade_binary.binary.size();
    si.pCode = reinterpret_cast<const uint32_t *>(grade_binary.binary.data());
    VkShaderModule shader{};
    tracked_vkCreateShaderModule(d_, &si, nullptr, &shader);
    VkComputePipelineCreateInfo ci{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = shader;
    ci.stage.pName = "main";
    ci.layout = pipelineLayout;
    VkPipeline pipeline{};
    VkResult vr = tracked_vkCreateComputePipelines(d_, VK_NULL_HANDLE, 1, &ci,
                                                   nullptr, &pipeline);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo pi{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    VkDescriptorPool pool{};
    create_descriptor_pool(&pi, &pool);
    VkDescriptorSetAllocateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    di.descriptorPool = pool;
    di.descriptorSetCount = 1;
    di.pSetLayouts = &layout;
    VkDescriptorSet set{};
    allocate_descriptor_sets(&di, &set);
    VkDescriptorBufferInfo db[2]{{bufs[0], 0, bytes}, {bufs[1], 0, bytes}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t k = 0; k < 2; k++) {
      writes[k] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[k].dstSet = set;
      writes[k].dstBinding = k;
      writes[k].descriptorCount = 1;
      writes[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[k].pBufferInfo = &db[k];
    }
    update_descriptors(2, writes);
    VkCommandBufferAllocateInfo cai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    allocate_command_buffers(&cai, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_command_buffer(cmd, &begin);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0, 1, &set, 0, nullptr);
    struct Push {
      float v[11];
      uint32_t n;
    } push{{p.exposure, p.contrast, p.gamma, p.lift, p.gain, p.offset,
            p.temperature, p.tint, p.saturation, p.vibrance, p.hue},
           pixel_count};
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(push), &push);
    dispatch(cmd, (push.n + 63) / 64, 1, 1);
    provenance_.command_recorded = true;
    provenance_.dispatch_or_draw_issued = true;
    end_command_buffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (vr == VK_SUCCESS)
      vr = queue_submit(&submit);
    if (vr == VK_SUCCESS)
      provenance_.queue_submission_issued = true;
    if (vr == VK_SUCCESS)
      vr = queue_wait_idle();
    if (vr == VK_SUCCESS)
      provenance_.synchronization_waited = true;
    if (vr == VK_SUCCESS) {
      vkMapMemory(d_, memory[1], 0, bytes, 0, &m);
      std::memcpy(out.data(), m, bytes);
      vkUnmapMemory(d_, memory[1]);
      provenance_.output_written = true;
      provenance_.readback_performed = true;
    }
    tracked_vkFreeCommandBuffers(d_, pool_, 1, &cmd);
    tracked_vkDestroyDescriptorPool(d_, pool, nullptr);
    tracked_vkDestroyPipeline(d_, pipeline, nullptr);
    tracked_vkDestroyShaderModule(d_, shader, nullptr);
    tracked_vkDestroyPipelineLayout(d_, pipelineLayout, nullptr);
    tracked_vkDestroyDescriptorSetLayout(d_, layout, nullptr);
    cleanup();
    provenance_.native_error_code = static_cast<std::int64_t>(vr);
    provenance_.cpu_color_reference_invocations =
        cpu_color_reference_count() -
        provenance_.cpu_color_reference_invocations;
    return vr == VK_SUCCESS ? DIGITOR_RESULT_OK
                            : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult
  execute_curves_rgba32f(std::span<const Color> src, std::span<Color> out,
                         const CompiledRgbCurves &curves) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_VULKAN, true, i_.device_name,
                           shader_compiler_.identity().c_str(),
                           "rgb_curves.hlsl:main",
                           "VkComputePipeline:rgb-curves-v1");
    provenance_.curves_enabled = true;
    provenance_.curve_lut_size = curves.lut_size();
    provenance_.compiled_curve_identity = curves.identity();
    provenance_.native_curve_shader_identity = "rgb_curves.hlsl:abi-v1";
    provenance_.native_lut_resource_identity =
        curves.identity() + ":" + i_.device_name;

    if (src.size() != out.size())
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (src.empty())
      return DIGITOR_RESULT_OK;
    std::uint32_t pixel_count = 0;
    if (!checked_size_to_uint32(src.size(), pixel_count))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{.source = digitor_rgb_curves_hlsl,
                                 .entry_point = "main",
                                 .source_name = "rgb_curves.hlsl",
                                 .target_profile = "cs_6_0",
                                 .stage = ShaderStage::compute,
                                 .backend = ShaderBackend::vulkan,
                                 .macros = {{"DIGITOR_VULKAN", "1"}},
                                 .include_roots = {},
                                 .specialization_constants = {},
                                 .optimization =
                                     ShaderOptimization::performance,
                                 .debug_info = false};
    const auto binary = vulkan_shader(request);
    if (!binary) {
      provenance_.failure_stage = "SPIR-V generation";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    provenance_.shader_pipeline_cache = CacheDisposition::Hit;
    VkBuffer buffers[4]{};
    VkDeviceMemory memories[4]{};
    VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};
    VkShaderModule module{};
    VkPipeline pipeline{};
    VkDescriptorPool descriptor_pool{};
    VkCommandBuffer command{};
    auto cleanup = [&] {
      if (command)
        tracked_vkFreeCommandBuffers(d_, pool_, 1, &command);
      if (descriptor_pool)
        tracked_vkDestroyDescriptorPool(d_, descriptor_pool, nullptr);
      if (pipeline)
        tracked_vkDestroyPipeline(d_, pipeline, nullptr);
      if (module)
        tracked_vkDestroyShaderModule(d_, module, nullptr);
      if (pipeline_layout)
        tracked_vkDestroyPipelineLayout(d_, pipeline_layout, nullptr);
      if (set_layout)
        tracked_vkDestroyDescriptorSetLayout(d_, set_layout, nullptr);
      for (int i = 0; i < 4; i++) {
        if (buffers[i])
          tracked_vkDestroyBuffer(d_, buffers[i], nullptr);
        if (memories[i])
          tracked_vkFreeMemory(d_, memories[i], nullptr);
      }
    };
    const auto lut = native_rgb_curves_lut(curves);
    const auto parameters = native_rgb_curves_parameters(curves, pixel_count);
    const VkDeviceSize sizes[]{src.size_bytes(), out.size_bytes(),
                               lut.size() * sizeof(float), sizeof(parameters)};
    for (int i = 0; i < 4; i++) {
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bi.size = sizes[i];
      bi.usage = i == 3 ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                        : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (tracked_vkCreateBuffer(d_, &bi, nullptr, &buffers[i]) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      VkMemoryRequirements req{};
      vkGetBufferMemoryRequirements(d_, buffers[i], &req);
      auto type =
          mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = type;
      if (type == UINT32_MAX ||
          tracked_vkAllocateMemory(d_, &ai, nullptr, &memories[i]) !=
              VK_SUCCESS ||
          vkBindBufferMemory(d_, buffers[i], memories[i], 0) != VK_SUCCESS) {
        cleanup();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
    void *m = nullptr;
    vkMapMemory(d_, memories[0], 0, sizes[0], 0, &m);
    std::memcpy(m, src.data(), sizes[0]);
    vkUnmapMemory(d_, memories[0]);
    vkMapMemory(d_, memories[2], 0, sizes[2], 0, &m);
    std::memcpy(m, lut.data(), sizes[2]);
    vkUnmapMemory(d_, memories[2]);
    vkMapMemory(d_, memories[3], 0, sizes[3], 0, &m);
    std::memcpy(m, &parameters, sizes[3]);
    vkUnmapMemory(d_, memories[3]);
    provenance_.source_upload_performed = true;
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo sli{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.bindingCount = 4;
    sli.pBindings = bindings;
    if (tracked_vkCreateDescriptorSetLayout(d_, &sli, nullptr, &set_layout) !=
        VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkPipelineLayoutCreateInfo pli{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout;
    if (tracked_vkCreatePipelineLayout(d_, &pli, nullptr, &pipeline_layout) !=
        VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = binary.binary.size();
    smi.pCode = reinterpret_cast<const uint32_t *>(binary.binary.data());
    if (tracked_vkCreateShaderModule(d_, &smi, nullptr, &module) !=
        VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkComputePipelineCreateInfo pci{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pci.stage.module = module;
    pci.stage.pName = "main";
    pci.layout = pipeline_layout;
    if (tracked_vkCreateComputePipelines(d_, VK_NULL_HANDLE, 1, &pci, nullptr,
                                         &pipeline) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorPoolSize pool_sizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                                       {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpi{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = pool_sizes;
    if (create_descriptor_pool(&dpi, &descriptor_pool) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorSetAllocateInfo dai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptor_pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout;
    VkDescriptorSet set{};
    if (allocate_descriptor_sets(&dai, &set) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkDescriptorBufferInfo infos[4]{};
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4; i++) {
      infos[i] = {buffers[i], 0, sizes[i]};
      writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[i].dstSet = set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                        : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].pBufferInfo = &infos[i];
    }
    update_descriptors(4, writes);
    provenance_.curve_source_bound = provenance_.curve_destination_bound =
        provenance_.curve_lut_bound = provenance_.curve_parameters_bound = true;
    provenance_.native_lut_cache = CacheDisposition::Miss;
    VkCommandBufferAllocateInfo cai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (allocate_command_buffers(&cai, &command) != VK_SUCCESS) {
      cleanup();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_command_buffer(command, &cbi);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout, 0, 1, &set, 0, nullptr);
    dispatch(command, (parameters.pixel_count + 63) / 64, 1, 1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    end_command_buffer(command);
    provenance_.command_recorded = provenance_.dispatch_or_draw_issued = true;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    VkResult result = queue_submit(&submit);
    if (result == VK_SUCCESS) {
      provenance_.queue_submission_issued = true;
      result = queue_wait_idle();
    }
    if (result == VK_SUCCESS) {
      provenance_.synchronization_waited = true;
      vkMapMemory(d_, memories[1], 0, sizes[1], 0, &m);
      std::memcpy(out.data(), m, sizes[1]);
      vkUnmapMemory(d_, memories[1]);
      provenance_.output_written = provenance_.readback_performed =
          provenance_.validation_readback_completed = true;
    }
    cleanup();
    provenance_.native_error_code = result;
    return result == VK_SUCCESS ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult create_texture(const DigitorTextureDesc &a,
                               void **o) noexcept override {
    *o = nullptr;
    auto *t = new (std::nothrow) VkTex{d_};
    if (!t)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    c.imageType = VK_IMAGE_TYPE_2D;
    c.extent = {a.width, a.height, 1};
    c.mipLevels = c.arrayLayers = 1;
    c.format = fmt(a.format);
    c.tiling = VK_IMAGE_TILING_OPTIMAL;
    c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    c.samples = VK_SAMPLE_COUNT_1_BIT;
    c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (a.usage & DIGITOR_TEXTURE_USAGE_SAMPLED)
      c.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (a.usage & DIGITOR_TEXTURE_USAGE_STORAGE)
      c.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (a.usage & DIGITOR_TEXTURE_USAGE_RENDER_TARGET)
      c.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (a.usage & DIGITOR_TEXTURE_USAGE_TRANSFER_SOURCE)
      c.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (a.usage & DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION)
      c.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (tracked_vkCreateImage(d_, &c, nullptr, &t->x) != VK_SUCCESS) {
      delete t;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    VkMemoryRequirements r{};
    vkGetImageMemoryRequirements(d_, t->x, &r);
    auto n = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = n;
    if (n == UINT32_MAX ||
        tracked_vkAllocateMemory(d_, &ai, nullptr, &t->m) != VK_SUCCESS ||
        vkBindImageMemory(d_, t->x, t->m, 0) != VK_SUCCESS) {
      if (t->m)
        tracked_vkFreeMemory(d_, t->m, nullptr);
      tracked_vkDestroyImage(d_, t->x, nullptr);
      delete t;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t->x;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = c.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (create_image_view(&vi, &t->v) != VK_SUCCESS) {
      tracked_vkFreeMemory(d_, t->m, nullptr);
      tracked_vkDestroyImage(d_, t->x, nullptr);
      delete t;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    *o = t;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult create_buffer(const DigitorBufferDesc &a,
                              void **o) noexcept override {
    *o = nullptr;
    auto *b = new (std::nothrow) VkBuf{d_};
    if (!b)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    c.size = a.size;
    c.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    c.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (a.usage & DIGITOR_BUFFER_USAGE_UNIFORM)
      c.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (a.usage & DIGITOR_BUFFER_USAGE_STORAGE)
      c.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (tracked_vkCreateBuffer(d_, &c, nullptr, &b->x) != VK_SUCCESS) {
      delete b;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    VkMemoryRequirements r{};
    vkGetBufferMemoryRequirements(d_, b->x, &r);
    auto host = (a.usage & (DIGITOR_BUFFER_USAGE_UPLOAD |
                            DIGITOR_BUFFER_USAGE_STAGING)) != 0;
    auto n = mem(r.memoryTypeBits, host ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = r.size;
    ai.memoryTypeIndex = n;
    if (n == UINT32_MAX ||
        tracked_vkAllocateMemory(d_, &ai, nullptr, &b->m) != VK_SUCCESS ||
        vkBindBufferMemory(d_, b->x, b->m, 0) != VK_SUCCESS) {
      if (b->m)
        tracked_vkFreeMemory(d_, b->m, nullptr);
      tracked_vkDestroyBuffer(d_, b->x, nullptr);
      delete b;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    *o = b;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult create_sampler(const DigitorSamplerDesc &a,
                               void **o) noexcept override {
    auto *s = new (std::nothrow) VkSamp{d_};
    if (!s)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    VkSamplerCreateInfo c{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    c.minFilter = a.min_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    c.magFilter = a.mag_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    c.mipmapMode = a.mip_filter ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    c.maxLod = VK_LOD_CLAMP_NONE;
    c.unnormalizedCoordinates = !a.normalized_coordinates;
    if (vkCreateSampler(d_, &c, nullptr, &s->x) != VK_SUCCESS) {
      delete s;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    *o = s;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult map_buffer(void *p, uint64_t offset, uint64_t size,
                           void **o) noexcept override {
    if (!p || !o)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto *b = (VkBuf *)p;
    *o = nullptr;
    VkResult r = vkMapMemory(d_, b->m, offset, size, 0, o);
    return r == VK_SUCCESS ? DIGITOR_RESULT_OK
                           : (r == VK_ERROR_OUT_OF_HOST_MEMORY ||
                                      r == VK_ERROR_OUT_OF_DEVICE_MEMORY
                                  ? DIGITOR_RESULT_OUT_OF_MEMORY
                                  : DIGITOR_RESULT_BACKEND_UNAVAILABLE);
  }
  void unmap_buffer(void *p) noexcept override {
    if (p)
      vkUnmapMemory(d_, ((VkBuf *)p)->m);
  }
  void destroy_texture(void *p) noexcept override {
    auto *t = (VkTex *)p;
    if (t) {
      tracked_vkDestroyImageView(d_, t->v, nullptr);
      tracked_vkDestroyImage(d_, t->x, nullptr);
      tracked_vkFreeMemory(d_, t->m, nullptr);
      delete t;
    }
  }
  void destroy_buffer(void *p) noexcept override {
    auto *b = (VkBuf *)p;
    if (b) {
      tracked_vkDestroyBuffer(d_, b->x, nullptr);
      tracked_vkFreeMemory(d_, b->m, nullptr);
      delete b;
    }
  }
  void destroy_sampler(void *p) noexcept override {
    auto *s = (VkSamp *)p;
    if (s) {
      vkDestroySampler(d_, s->x, nullptr);
      delete s;
    }
  }
};
} // namespace
std::unique_ptr<IRenderBackend> create_vulkan_backend() {
  VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  a.pApplicationName = "DigitorEngine";
  a.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo c{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  c.pApplicationInfo = &a;
  const char *validation = "VK_LAYER_KHRONOS_validation";
  if (gpu_validation_requested()) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto &layer : layers)
      if (std::strcmp(layer.layerName, validation) == 0) {
        c.enabledLayerCount = 1;
        c.ppEnabledLayerNames = &validation;
        break;
      }
  }
  VkInstance in;
  if (vkCreateInstance(&c, nullptr, &in) != VK_SUCCESS)
    return nullptr;
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(in, &n, nullptr);
  if (!n) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
  std::vector<VkPhysicalDevice> p(n);
  vkEnumeratePhysicalDevices(in, &n, p.data());
  uint32_t qn = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(p[0], &qn, nullptr);
  std::vector<VkQueueFamilyProperties> q(qn);
  vkGetPhysicalDeviceQueueFamilyProperties(p[0], &qn, q.data());
  uint32_t qi = UINT32_MAX;
  for (uint32_t x = 0; x < qn; x++)
    if (q[x].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      qi = x;
      break;
    }
  if (qi == UINT32_MAX) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
  float priority = 1;
  VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qc.queueFamilyIndex = qi;
  qc.queueCount = 1;
  qc.pQueuePriorities = &priority;
  VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dc.queueCreateInfoCount = 1;
  dc.pQueueCreateInfos = &qc;
  VkDevice d;
  if (vkCreateDevice(p[0], &dc, nullptr, &d) != VK_SUCCESS) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
  return std::make_unique<VulkanBackend>(in, p[0], d, qi);
}
} // namespace digitor
