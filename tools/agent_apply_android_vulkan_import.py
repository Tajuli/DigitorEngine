from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1))


root = Path(__file__).resolve().parents[1]
vulkan = root / "src/gpu/vulkan_backend.cpp"
cmake = root / "cmake/DigitorEngineBase.cmake"
physical_workflow = root / ".github/workflows/android-physical-gpu-qualification.yml"
shader = root / "src/gpu/shaders/android_ahardwarebuffer_yuv_to_rgba16f.comp"

# Android Vulkan platform declarations must be visible before vulkan.h.
replace_once(
    vulkan,
    "#include <windows.h>\n#endif\n#include <vulkan/vulkan.h>\n",
    "#include <windows.h>\n#endif\n#if defined(__ANDROID__)\n#ifndef VK_USE_PLATFORM_ANDROID_KHR\n#define VK_USE_PLATFORM_ANDROID_KHR\n#endif\n#include <android/hardware_buffer.h>\n#include <unistd.h>\n#endif\n#include <vulkan/vulkan.h>\n",
)

# Embed the production Android AHardwareBuffer conversion shader in the same
# Android native artifact as the rest of the Vulkan backend.
replace_once(
    vulkan,
    '#include "color_pipeline_buffer.hpp"\n#endif\n',
    '#include "color_pipeline_buffer.hpp"\n#if defined(__ANDROID__)\n#include "android_ahardwarebuffer_yuv_to_rgba16f.hpp"\n#endif\n#endif\n',
)
replace_once(
    vulkan,
    '''  } else if (request.source_name == "color_pipeline.hlsl") {\n    words = digitor_color_pipeline_buffer_spirv;\n    word_count = digitor_color_pipeline_buffer_spirv_word_count;\n  }\n''',
    '''  } else if (request.source_name == "color_pipeline.hlsl") {\n    words = digitor_color_pipeline_buffer_spirv;\n    word_count = digitor_color_pipeline_buffer_spirv_word_count;\n#if defined(__ANDROID__)\n  } else if (request.source_name ==\n             "android_ahardwarebuffer_yuv_to_rgba16f.comp") {\n    words = digitor_android_ahardwarebuffer_yuv_to_rgba16f_spirv;\n    word_count =\n        digitor_android_ahardwarebuffer_yuv_to_rgba16f_spirv_word_count;\n#endif\n  }\n''',
)

android_import = r'''
#if defined(__ANDROID__)
  DigitorResult import_android_ahardwarebuffer(
      const ZeroCopyImportRequest& request,
      ProcessedGpuFramePtr& output) noexcept {
    output.reset();
    const auto surface = request.surface;
    if (!surface || request.renderer_backend != DIGITOR_RENDERER_VULKAN ||
        request.output_format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
        request.working_color_space != "linear-rgba")
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    const auto& descriptor = surface->descriptor();
    if (descriptor.platform != NativeMediaPlatform::android ||
        descriptor.handle_type != NativeMediaHandleType::ahardware_buffer ||
        (descriptor.pixel_format != NativeMediaPixelFormat::nv12 &&
         descriptor.pixel_format != NativeMediaPixelFormat::p010) ||
        !descriptor.width || !descriptor.height || (descriptor.width & 1u) ||
        (descriptor.height & 1u) || descriptor.plane_count != 2 ||
        !descriptor.native_handle)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (descriptor.acquire_sync.type != NativeMediaSyncType::none &&
        descriptor.acquire_sync.type != NativeMediaSyncType::sync_fd)
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    auto* ahb = reinterpret_cast<AHardwareBuffer*>(descriptor.native_handle);
    AHardwareBuffer_Desc ahb_descriptor{};
    AHardwareBuffer_describe(ahb, &ahb_descriptor);
    if (ahb_descriptor.width != descriptor.width ||
        ahb_descriptor.height != descriptor.height ||
        ahb_descriptor.layers != 1 ||
        (ahb_descriptor.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) == 0)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    const auto get_ahb_properties =
        reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            vkGetDeviceProcAddr(
                d_, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    const auto import_semaphore_fd =
        reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
            vkGetDeviceProcAddr(d_, "vkImportSemaphoreFdKHR"));
    if (!get_ahb_properties || !import_semaphore_fd)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkAndroidHardwareBufferFormatPropertiesANDROID format_properties{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID properties{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    properties.pNext = &format_properties;
    if (get_ahb_properties(d_, ahb, &properties) != VK_SUCCESS ||
        properties.allocationSize == 0 || properties.memoryTypeBits == 0 ||
        (format_properties.format == VK_FORMAT_UNDEFINED &&
         format_properties.externalFormat == 0))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkImage input_image{};
    VkDeviceMemory input_memory{};
    VkSamplerYcbcrConversion conversion{};
    VkImageView input_view{};
    VkSampler input_sampler{};
    VkSemaphore acquire_semaphore{};
    VkDescriptorSetLayout descriptor_layout{};
    VkPipelineLayout pipeline_layout{};
    VkShaderModule shader_module{};
    VkPipeline pipeline{};
    VkDescriptorPool descriptor_pool{};
    VkCommandBuffer command{};

    auto cleanup = [&]() noexcept {
      if (command) tracked_vkFreeCommandBuffers(d_, pool_, 1, &command);
      if (descriptor_pool)
        tracked_vkDestroyDescriptorPool(d_, descriptor_pool, nullptr);
      if (pipeline) tracked_vkDestroyPipeline(d_, pipeline, nullptr);
      if (shader_module)
        tracked_vkDestroyShaderModule(d_, shader_module, nullptr);
      if (pipeline_layout)
        tracked_vkDestroyPipelineLayout(d_, pipeline_layout, nullptr);
      if (descriptor_layout)
        tracked_vkDestroyDescriptorSetLayout(d_, descriptor_layout, nullptr);
      if (acquire_semaphore)
        vkDestroySemaphore(d_, acquire_semaphore, nullptr);
      if (input_sampler) vkDestroySampler(d_, input_sampler, nullptr);
      if (input_view) tracked_vkDestroyImageView(d_, input_view, nullptr);
      if (conversion)
        vkDestroySamplerYcbcrConversion(d_, conversion, nullptr);
      if (input_image) tracked_vkDestroyImage(d_, input_image, nullptr);
      if (input_memory) tracked_vkFreeMemory(d_, input_memory, nullptr);
    };
    struct CleanupGuard final {
      decltype(cleanup)& callback;
      ~CleanupGuard() { callback(); }
    } cleanup_guard{cleanup};

    VkExternalFormatANDROID external_format{
        VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    external_format.externalFormat = format_properties.externalFormat;
    VkExternalMemoryImageCreateInfo external_memory{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external_memory.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    if (format_properties.format == VK_FORMAT_UNDEFINED)
      external_memory.pNext = &external_format;

    VkImageCreateInfo input_create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    input_create.pNext = &external_memory;
    input_create.imageType = VK_IMAGE_TYPE_2D;
    input_create.format = format_properties.format;
    input_create.extent = {descriptor.width, descriptor.height, 1};
    input_create.mipLevels = 1;
    input_create.arrayLayers = 1;
    input_create.samples = VK_SAMPLE_COUNT_1_BIT;
    input_create.tiling = VK_IMAGE_TILING_OPTIMAL;
    input_create.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    input_create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    input_create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (tracked_vkCreateImage(d_, &input_create, nullptr, &input_image) !=
        VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    const auto memory_type = mem(properties.memoryTypeBits, 0);
    if (memory_type == UINT32_MAX)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    VkImportAndroidHardwareBufferInfoANDROID import_info{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    import_info.buffer = ahb;
    VkMemoryDedicatedAllocateInfo dedicated{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.pNext = &import_info;
    dedicated.image = input_image;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &dedicated;
    allocation.allocationSize = properties.allocationSize;
    allocation.memoryTypeIndex = memory_type;
    if (tracked_vkAllocateMemory(d_, &allocation, nullptr, &input_memory) !=
            VK_SUCCESS ||
        vkBindImageMemory(d_, input_image, input_memory, 0) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkSamplerYcbcrConversionCreateInfo conversion_create{
        VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    conversion_create.format = format_properties.format;
    conversion_create.ycbcrModel = format_properties.suggestedYcbcrModel;
    conversion_create.ycbcrRange = format_properties.suggestedYcbcrRange;
    conversion_create.components =
        format_properties.samplerYcbcrConversionComponents;
    conversion_create.xChromaOffset = format_properties.suggestedXChromaOffset;
    conversion_create.yChromaOffset = format_properties.suggestedYChromaOffset;
    conversion_create.chromaFilter =
        (format_properties.formatFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
            ? VK_FILTER_LINEAR
            : VK_FILTER_NEAREST;
    if (format_properties.format == VK_FORMAT_UNDEFINED)
      conversion_create.pNext = &external_format;
    if (vkCreateSamplerYcbcrConversion(
            d_, &conversion_create, nullptr, &conversion) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkSamplerYcbcrConversionInfo conversion_info{
        VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    conversion_info.conversion = conversion;
    VkImageViewCreateInfo view_create{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_create.pNext = &conversion_info;
    view_create.image = input_image;
    view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create.format = format_properties.format;
    view_create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (tracked_vkCreateImageView(d_, &view_create, nullptr, &input_view) !=
        VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkSamplerCreateInfo sampler_create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_create.pNext = &conversion_info;
    sampler_create.magFilter = conversion_create.chromaFilter;
    sampler_create.minFilter = conversion_create.chromaFilter;
    sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.maxLod = 1.0f;
    if (vkCreateSampler(d_, &sampler_create, nullptr, &input_sampler) !=
        VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    auto owner = std::shared_ptr<VkPreviewOwner>(
        new (std::nothrow) VkPreviewOwner{});
    if (!owner) return DIGITOR_RESULT_OUT_OF_MEMORY;
    owner->device = d_;
    owner->device_live = device_live_;
    owner->upstream = surface;
    owner->output_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (create_node_image(
            descriptor.width, descriptor.height,
            VK_FORMAT_R16G16B16A16_SFLOAT, owner->output,
            owner->output_memory, owner->output_view) != DIGITOR_RESULT_OK)
      return DIGITOR_RESULT_OUT_OF_MEMORY;

    ShaderCompileRequest shader_request{
        .source = "",
        .entry_point = "main",
        .source_name = "android_ahardwarebuffer_yuv_to_rgba16f.comp",
        .target_profile = "spirv-v1.1",
        .stage = ShaderStage::compute,
        .backend = ShaderBackend::vulkan,
        .macros = {},
        .include_roots = {},
        .specialization_constants = {},
        .optimization = ShaderOptimization::performance,
        .debug_info = false};
    const auto binary = vulkan_shader(shader_request);
    if (!binary || binary.binary.empty())
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = &input_sampler;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptor_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_info.bindingCount = 2;
    descriptor_info.pBindings = bindings;
    if (tracked_vkCreateDescriptorSetLayout(
            d_, &descriptor_info, nullptr, &descriptor_layout) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkPipelineLayoutCreateInfo layout_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_layout;
    if (tracked_vkCreatePipelineLayout(
            d_, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkShaderModuleCreateInfo shader_info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize = binary.binary.size();
    shader_info.pCode =
        reinterpret_cast<const std::uint32_t*>(binary.binary.data());
    if (tracked_vkCreateShaderModule(
            d_, &shader_info, nullptr, &shader_module) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader_module;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline_layout;
    if (tracked_vkCreateComputePipelines(
            d_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) !=
        VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkDescriptorPoolSize pool_sizes[2]{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    if (tracked_vkCreateDescriptorPool(
            d_, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkDescriptorSet descriptor_set{};
    VkDescriptorSetAllocateInfo set_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &descriptor_layout;
    if (tracked_vkAllocateDescriptorSets(d_, &set_info, &descriptor_set) !=
        VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkDescriptorImageInfo input_info{
        input_sampler, input_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo output_info{
        VK_NULL_HANDLE, owner->output_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &input_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &output_info;
    vkUpdateDescriptorSets(d_, 2, writes, 0, nullptr);

    VkCommandBufferAllocateInfo command_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = pool_;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    if (tracked_vkAllocateCommandBuffers(d_, &command_info, &command) !=
        VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkImageMemoryBarrier acquire_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    acquire_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    acquire_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    acquire_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    acquire_barrier.dstQueueFamilyIndex = family_;
    acquire_barrier.srcAccessMask = 0;
    acquire_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    acquire_barrier.image = input_image;
    acquire_barrier.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier output_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    output_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    output_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    output_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    output_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    output_barrier.srcAccessMask = 0;
    output_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    output_barrier.image = owner->output;
    output_barrier.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier initial_barriers[2]{
        acquire_barrier, output_barrier};
    vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2,
        initial_barriers);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
        &descriptor_set, 0, nullptr);
    vkCmdDispatch(command, (descriptor.width + 7u) / 8u,
                  (descriptor.height + 7u) / 8u, 1);

    VkImageMemoryBarrier output_ready{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    output_ready.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    output_ready.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    output_ready.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    output_ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    output_ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    output_ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    output_ready.image = owner->output;
    output_ready.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier release_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    release_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    release_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    release_barrier.srcQueueFamilyIndex = family_;
    release_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    release_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    release_barrier.dstAccessMask = 0;
    release_barrier.image = input_image;
    release_barrier.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier final_barriers[2]{
        output_ready, release_barrier};
    vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2,
        final_barriers);
    owner->output_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (vkEndCommandBuffer(command) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    if (descriptor.acquire_sync.type == NativeMediaSyncType::sync_fd) {
      const int source_fd =
          static_cast<int>(descriptor.acquire_sync.handle);
      const int imported_fd = ::dup(source_fd);
      if (imported_fd < 0)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      VkSemaphoreCreateInfo semaphore_create{
          VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      if (vkCreateSemaphore(
              d_, &semaphore_create, nullptr, &acquire_semaphore) != VK_SUCCESS) {
        ::close(imported_fd);
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      VkImportSemaphoreFdInfoKHR import_sync{
          VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
      import_sync.semaphore = acquire_semaphore;
      import_sync.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
      import_sync.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      import_sync.fd = imported_fd;
      if (import_semaphore_fd(d_, &import_sync) != VK_SUCCESS) {
        ::close(imported_fd);
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      submit.waitSemaphoreCount = 1;
      submit.pWaitSemaphores = &acquire_semaphore;
      submit.pWaitDstStageMask = &wait_stage;
    }

    if (vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(queue_) != VK_SUCCESS)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    static std::atomic_uint64_t identities{1'200'000};
    output = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_VULKAN,
        GpuFrameMetadata{descriptor.width, descriptor.height,
                         DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,
                         GpuFrameAlpha::straight, descriptor.timestamp_us,
                         request.working_color_space},
        identities++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), false);
    bind_frame_context_lifetime(output);
    return output && output->ready() &&
                   output->has_context_identity(this)
               ? DIGITOR_RESULT_OK
               : DIGITOR_RESULT_INTERNAL_ERROR;
  }
#endif

'''

anchor = '''  DigitorResult import_native_media(const ZeroCopyImportRequest& request,\n                                    ProcessedGpuFramePtr& output) noexcept {\n'''
replace_once(vulkan, anchor, android_import + anchor)
replace_once(
    vulkan,
    '''    output.reset();\n#if !defined(_WIN32)\n    (void)request;\n    return DIGITOR_RESULT_UNSUPPORTED;\n#else\n''',
    '''    output.reset();\n#if defined(__ANDROID__)\n    return import_android_ahardwarebuffer(request, output);\n#elif defined(_WIN32)\n''',
)

# Android Vulkan production selection must enable every capability used by the
# MediaCodec/AHardwareBuffer import path on the exact selected VkDevice.
replace_once(
    vulkan,
    '''  a.apiVersion = VK_API_VERSION_1_0;\n''',
    '''#if defined(__ANDROID__)\n  a.apiVersion = VK_API_VERSION_1_1;\n#else\n  a.apiVersion = VK_API_VERSION_1_0;\n#endif\n''',
)

android_device_requirements = r'''#if defined(__ANDROID__)
  const std::array<const char*, 4> required_android_device_extensions{
      VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
      VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
      VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
  std::uint32_t android_extension_count = 0;
  if (vkEnumerateDeviceExtensionProperties(
          p[0], nullptr, &android_extension_count, nullptr) != VK_SUCCESS) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
  std::vector<VkExtensionProperties> android_extensions(
      android_extension_count);
  if (vkEnumerateDeviceExtensionProperties(
          p[0], nullptr, &android_extension_count,
          android_extensions.data()) != VK_SUCCESS ||
      !std::all_of(
          required_android_device_extensions.begin(),
          required_android_device_extensions.end(), [&](const char* name) {
            return std::any_of(
                android_extensions.begin(), android_extensions.end(),
                [&](const auto& item) {
                  return std::strcmp(item.extensionName, name) == 0;
                });
          })) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
  VkPhysicalDeviceSamplerYcbcrConversionFeatures android_ycbcr{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
  VkPhysicalDeviceFeatures2 android_features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  android_features.pNext = &android_ycbcr;
  vkGetPhysicalDeviceFeatures2(p[0], &android_features);
  if (!android_ycbcr.samplerYcbcrConversion) {
    vkDestroyInstance(in, nullptr);
    return nullptr;
  }
#endif
'''
replace_once(
    vulkan,
    '''#if defined(_WIN32)\n  const std::array<const char*, 9> required_device_extensions{\n''',
    android_device_requirements + '''#if defined(_WIN32)\n  const std::array<const char*, 9> required_device_extensions{\n''',
)
replace_once(
    vulkan,
    '''#if defined(_WIN32)\n  dc.pNext = &ycbcr;\n  dc.enabledExtensionCount =\n      static_cast<std::uint32_t>(required_device_extensions.size());\n  dc.ppEnabledExtensionNames = required_device_extensions.data();\n#endif\n''',
    '''#if defined(_WIN32)\n  dc.pNext = &ycbcr;\n  dc.enabledExtensionCount =\n      static_cast<std::uint32_t>(required_device_extensions.size());\n  dc.ppEnabledExtensionNames = required_device_extensions.data();\n#elif defined(__ANDROID__)\n  dc.pNext = &android_ycbcr;\n  dc.enabledExtensionCount = static_cast<std::uint32_t>(\n      required_android_device_extensions.size());\n  dc.ppEnabledExtensionNames = required_android_device_extensions.data();\n#endif\n''',
)

# Build the GLSL conversion shader reproducibly with the NDK glslc and embed it
# into the Android engine artifact.
replace_once(
    cmake,
    '''    function(digitor_add_embedded_spirv NAME SOURCE SYMBOL)\n''',
    '''    function(digitor_add_embedded_glsl_spirv NAME SOURCE SYMBOL)\n        set(SPV "${_DIGITOR_SPIRV_DIR}/${NAME}.spv")\n        set(HEADER "${_DIGITOR_SPIRV_DIR}/${NAME}.hpp")\n        add_custom_command(\n            OUTPUT "${SPV}"\n            COMMAND "${DIGITOR_ANDROID_GLSLC}"\n                    -fshader-stage=compute --target-env=vulkan1.1 -O\n                    "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" -o "${SPV}"\n            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"\n            VERBATIM)\n        add_custom_command(\n            OUTPUT "${HEADER}"\n            COMMAND "${CMAKE_COMMAND}"\n                    -DINPUT=${SPV} -DOUTPUT=${HEADER} -DSYMBOL=${SYMBOL}\n                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"\n            DEPENDS "${SPV}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"\n            VERBATIM)\n        set(DIGITOR_EMBEDDED_SPIRV_HEADERS ${DIGITOR_EMBEDDED_SPIRV_HEADERS} "${HEADER}" PARENT_SCOPE)\n    endfunction()\n\n    function(digitor_add_embedded_spirv NAME SOURCE SYMBOL)\n''',
)
replace_once(
    cmake,
    '''    digitor_add_embedded_spirv(node_masked_composite src/gpu/shaders/node_masked_composite.hlsl digitor_node_masked_composite_spirv)\n\n    add_custom_target(digitor_android_spirv DEPENDS ${DIGITOR_EMBEDDED_SPIRV_HEADERS})\n''',
    '''    digitor_add_embedded_spirv(node_masked_composite src/gpu/shaders/node_masked_composite.hlsl digitor_node_masked_composite_spirv)\n    digitor_add_embedded_glsl_spirv(android_ahardwarebuffer_yuv_to_rgba16f src/gpu/shaders/android_ahardwarebuffer_yuv_to_rgba16f.comp digitor_android_ahardwarebuffer_yuv_to_rgba16f_spirv)\n\n    add_custom_target(digitor_android_spirv DEPENDS ${DIGITOR_EMBEDDED_SPIRV_HEADERS})\n''',
)

shader.parent.mkdir(parents=True, exist_ok=True)
shader.write_text(
    '''#version 450\n\nlayout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\nlayout(set = 0, binding = 0) uniform sampler2D decodedFrame;\nlayout(rgba16f, set = 0, binding = 1) writeonly uniform image2D rgbaOutput;\n\nvoid main() {\n    ivec2 size = imageSize(rgbaOutput);\n    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);\n    if (pixel.x >= size.x || pixel.y >= size.y) return;\n    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);\n    vec4 converted = texture(decodedFrame, uv);\n    imageStore(rgbaOutput, pixel, vec4(converted.rgb, 1.0));\n}\n'''
)

# Make the physical Android qualification workflow react to production importer
# changes and compile the actual engine artifact for arm64 in addition to its
# standalone device harness.
replace_once(
    physical_workflow,
    "      - 'src/gpu/android_gpu_qualification.cpp'\n",
    "      - 'src/gpu/android_gpu_qualification.cpp'\n"
    "      - 'src/gpu/vulkan_backend.cpp'\n"
    "      - 'src/gpu/shaders/android_ahardwarebuffer_yuv_to_rgba16f.comp'\n"
    "      - 'cmake/DigitorEngineBase.cmake'\n",
)
replace_once(
    physical_workflow,
    '''      - name: Build Android arm64 physical harness\n        run: cmake --build build/android-physical-runtime --parallel 2\n\n  physical-device:\n''',
    '''      - name: Build Android arm64 physical harness\n        run: cmake --build build/android-physical-runtime --parallel 2\n      - name: Configure production engine for Android arm64\n        run: |\n          cmake -S . -B build/android-production-engine -G Ninja \\\n            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \\\n            -DANDROID_ABI=arm64-v8a \\\n            -DANDROID_PLATFORM=android-31 \\\n            -DANDROID_STL=c++_static \\\n            -DCMAKE_BUILD_TYPE=Release \\\n            -DDIGITOR_BUILD_TESTS=OFF \\\n            -DDIGITOR_BUILD_EXAMPLES=OFF \\\n            -DDIGITOR_ENABLE_FFMPEG=OFF\n      - name: Build production engine for Android arm64\n        run: cmake --build build/android-production-engine --parallel 2\n\n  physical-device:\n''',
)

print("Android Vulkan AHardwareBuffer production import patch applied")
