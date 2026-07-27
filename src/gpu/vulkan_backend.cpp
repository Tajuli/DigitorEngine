#include "core/string_utils.hpp"
#include "core/numeric_utils.hpp"
#include "gpu/gpu_backend.hpp"
#include "gpu/native_rgb_curves.hpp"
#include "digitor/shader.hpp"
#include "rgb_curves_shader.hpp"
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>
namespace digitor {
namespace {
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
struct VkPreviewOwner { VkDevice device{};VkImage source{},output{},preview{};VkDeviceMemory source_memory{},output_memory{},preview_memory{};VkImageView source_view{},output_view{};
 ~VkPreviewOwner(){if(!device)return;if(source_view)vkDestroyImageView(device,source_view,nullptr);if(output_view)vkDestroyImageView(device,output_view,nullptr);for(auto i:{source,output,preview})if(i)vkDestroyImage(device,i,nullptr);for(auto m:{source_memory,output_memory,preview_memory})if(m)vkFreeMemory(device,m,nullptr);}
};
class VulkanBackend final : public IRenderBackend {
  VkInstance in_{};
  VkPhysicalDevice ph_{};
  VkDevice d_{};
  VkQueue queue_{};
  uint32_t family_{};
  VkCommandPool pool_{};
  VkPhysicalDeviceMemoryProperties mp_{};
  ShaderCompiler shader_compiler_{};
  ShaderCache shader_cache_{};
  DigitorRendererInfo i_{};
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

public:
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
  }
  DigitorRendererInfo info() const noexcept override { return i_; }
  DigitorResult execute_process_curves_gpu(std::span<const Color>src,uint32_t width,uint32_t height,int64_t timestamp,const CompiledRgbCurves&curves,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();begin_grade_provenance(DIGITOR_RENDERER_VULKAN,true,i_.device_name,shader_compiler_.identity().c_str(),"rgb_curves.hlsl:texture","VkImage RGB curves");if(!width||!height||src.size()!=size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{.source=digitor_rgb_curves_hlsl,.entry_point="main",.source_name="rgb_curves.hlsl",.target_profile="cs_6_0",.stage=ShaderStage::compute,.backend=ShaderBackend::vulkan,.macros={{"DIGITOR_VULKAN","1"},{"DIGITOR_TEXTURE_OUTPUT","1"}}};auto binary=shader_cache_.get_or_compile(shader_compiler_,request);if(!binary)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner=std::shared_ptr<VkPreviewOwner>(new(std::nothrow)VkPreviewOwner{});if(!owner)return DIGITOR_RESULT_OUT_OF_MEMORY;owner->device=d_;
    auto image=[&](VkImageUsageFlags usage,VkImage&i,VkDeviceMemory&m){VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};c.imageType=VK_IMAGE_TYPE_2D;c.extent={width,height,1};c.mipLevels=c.arrayLayers=1;c.format=VK_FORMAT_R32G32B32A32_SFLOAT;c.tiling=VK_IMAGE_TILING_OPTIMAL;c.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;c.samples=VK_SAMPLE_COUNT_1_BIT;c.sharingMode=VK_SHARING_MODE_EXCLUSIVE;c.usage=usage;if(vkCreateImage(d_,&c,nullptr,&i)!=VK_SUCCESS)return false;VkMemoryRequirements r{};vkGetImageMemoryRequirements(d_,i,&r);auto mt=mem(r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=mt;return mt!=UINT32_MAX&&vkAllocateMemory(d_,&a,nullptr,&m)==VK_SUCCESS&&vkBindImageMemory(d_,i,m,0)==VK_SUCCESS;};
    if(!image(VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,owner->source,owner->source_memory)||!image(VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,owner->output,owner->output_memory)||!image(VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,owner->preview,owner->preview_memory))return DIGITOR_RESULT_OUT_OF_MEMORY;
    auto view=[&](VkImage i,VkImageView&v){VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};c.image=i;c.viewType=VK_IMAGE_VIEW_TYPE_2D;c.format=VK_FORMAT_R32G32B32A32_SFLOAT;c.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return vkCreateImageView(d_,&c,nullptr,&v)==VK_SUCCESS;};if(!view(owner->source,owner->source_view)||!view(owner->output,owner->output_view))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto lut=native_rgb_curves_lut(curves);auto params=native_rgb_curves_parameters(curves,uint32_t(src.size()));params.padding[0]=width;params.padding[1]=height;VkBuffer b[3]{};VkDeviceMemory bm[3]{};VkDeviceSize sizes[]{src.size_bytes(),lut.size()*sizeof(float),sizeof(params)};auto cleanup=[&]{for(int n=0;n<3;n++){if(b[n])vkDestroyBuffer(d_,b[n],nullptr);if(bm[n])vkFreeMemory(d_,bm[n],nullptr);}};
    for(int n=0;n<3;n++){VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.size=sizes[n];c.usage=n?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:VK_BUFFER_USAGE_TRANSFER_SRC_BIT;if(n==2)c.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;c.sharingMode=VK_SHARING_MODE_EXCLUSIVE;if(vkCreateBuffer(d_,&c,nullptr,&b[n])!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_OUT_OF_MEMORY;}VkMemoryRequirements r{};vkGetBufferMemoryRequirements(d_,b[n],&r);auto mt=mem(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=mt;if(mt==UINT32_MAX||vkAllocateMemory(d_,&a,nullptr,&bm[n])!=VK_SUCCESS||vkBindBufferMemory(d_,b[n],bm[n],0)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_OUT_OF_MEMORY;}}
    void*m=nullptr;for(int n=0;n<3;n++){vkMapMemory(d_,bm[n],0,sizes[n],0,&m);std::memcpy(m,n==0?static_cast<const void*>(src.data()):n==1?static_cast<const void*>(lut.data()):static_cast<const void*>(&params),sizes[n]);vkUnmapMemory(d_,bm[n]);}
    VkDescriptorSetLayoutBinding bindings[4]{};bindings[0]={0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};bindings[1]={1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};bindings[2]={2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};bindings[3]={3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};VkDescriptorSetLayout sl{};VkPipelineLayout pl{};VkShaderModule sm{};VkPipeline pipe{};VkDescriptorPool dp{};VkCommandBuffer cmd{};auto finish=[&]{if(cmd)vkFreeCommandBuffers(d_,pool_,1,&cmd);if(dp)vkDestroyDescriptorPool(d_,dp,nullptr);if(pipe)vkDestroyPipeline(d_,pipe,nullptr);if(sm)vkDestroyShaderModule(d_,sm,nullptr);if(pl)vkDestroyPipelineLayout(d_,pl,nullptr);if(sl)vkDestroyDescriptorSetLayout(d_,sl,nullptr);cleanup();};
    VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=4;si.pBindings=bindings;if(vkCreateDescriptorSetLayout(d_,&si,nullptr,&sl)!=VK_SUCCESS)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pi.setLayoutCount=1;pi.pSetLayouts=&sl;if(vkCreatePipelineLayout(d_,&pi,nullptr,&pl)!=VK_SUCCESS){finish();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=binary.binary.size();mi.pCode=reinterpret_cast<const uint32_t*>(binary.binary.data());if(vkCreateShaderModule(d_,&mi,nullptr,&sm)!=VK_SUCCESS){finish();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=sm;ci.stage.pName="main";ci.layout=pl;if(vkCreateComputePipelines(d_,VK_NULL_HANDLE,1,&ci,nullptr,&pipe)!=VK_SUCCESS){finish();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    VkDescriptorPoolSize ps[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};di.maxSets=1;di.poolSizeCount=4;di.pPoolSizes=ps;if(vkCreateDescriptorPool(d_,&di,nullptr,&dp)!=VK_SUCCESS){finish();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=dp;ai.descriptorSetCount=1;ai.pSetLayouts=&sl;VkDescriptorSet set{};if(vkAllocateDescriptorSets(d_,&ai,&set)!=VK_SUCCESS){finish();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkDescriptorImageInfo ii[2]{{VK_NULL_HANDLE,owner->source_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,owner->output_view,VK_IMAGE_LAYOUT_GENERAL}};VkDescriptorBufferInfo bi[2]{{b[1],0,sizes[1]},{b[2],0,sizes[2]}};VkWriteDescriptorSet w[4]{};for(int n=0;n<4;n++){w[n]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w[n].dstSet=set;w[n].dstBinding=n;w[n].descriptorCount=1;}w[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w[0].pImageInfo=&ii[0];w[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w[1].pImageInfo=&ii[1];w[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[2].pBufferInfo=&bi[0];w[3].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w[3].pBufferInfo=&bi[1];vkUpdateDescriptorSets(d_,4,w,0,nullptr);
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool_;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;if(vkAllocateCommandBuffers(d_,&ca,&cmd)!=VK_SUCCESS){finish();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(cmd,&cb);auto barrier=[&](VkImage image,VkImageLayout old,VkImageLayout now,VkAccessFlags srca,VkAccessFlags dsta,VkPipelineStageFlags srcs,VkPipelineStageFlags dsts){VkImageMemoryBarrier x{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};x.oldLayout=old;x.newLayout=now;x.srcAccessMask=srca;x.dstAccessMask=dsta;x.image=image;x.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(cmd,srcs,dsts,0,0,nullptr,0,nullptr,1,&x);};barrier(owner->source,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};vkCmdCopyBufferToImage(cmd,b[0],owner->source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);barrier(owner->source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);barrier(owner->output,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipe);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&set,0,nullptr);vkCmdDispatch(cmd,(uint32_t(src.size())+63)/64,1,1);barrier(owner->output,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);vkEndCommandBuffer(cmd);VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cmd;auto vr=vkQueueSubmit(queue_,1,&sub,VK_NULL_HANDLE);if(vr==VK_SUCCESS)vr=vkQueueWaitIdle(queue_);finish();if(vr!=VK_SUCCESS)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{1};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_VULKAN,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"linear-rgba"},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.readback_performed=false;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_present_gpu_frame(const ProcessedGpuFramePtr&frame)noexcept override{if(!frame||frame->acquire(this,DIGITOR_RENDERER_VULKAN)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;auto o=std::static_pointer_cast<VkPreviewOwner>(native_owner(*frame));VkCommandBuffer cmd{};VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};a.commandPool=pool_;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;a.commandBufferCount=1;if(!o||vkAllocateCommandBuffers(d_,&a,&cmd)!=VK_SUCCESS){(void)frame->release(this);return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(cmd,&b);VkImageMemoryBarrier bars[2]{};for(auto&x:bars){x={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};x.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}bars[0].image=o->output;bars[0].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;bars[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;bars[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;bars[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;bars[1].image=o->preview;bars[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;bars[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;bars[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,bars);auto m=frame->metadata();VkImageCopy c{};c.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.dstSubresource=c.srcSubresource;c.extent={m.width,m.height,1};vkCmdCopyImage(cmd,o->output,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,o->preview,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);bars[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;bars[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;bars[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;bars[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;bars[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;bars[1].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;bars[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;bars[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,2,bars);vkEndCommandBuffer(cmd);VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&cmd;auto v=vkQueueSubmit(queue_,1,&s,VK_NULL_HANDLE);if(v==VK_SUCCESS)v=vkQueueWaitIdle(queue_);vkFreeCommandBuffers(d_,pool_,1,&cmd);(void)frame->release(this);return v==VK_SUCCESS?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
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
      if (vkCreateBuffer(d_, &c, nullptr, &b) != VK_SUCCESS)
        return false;
      VkMemoryRequirements r{};
      vkGetBufferMemoryRequirements(d_, b, &r);
      auto n = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = n;
      return n != UINT32_MAX &&
             vkAllocateMemory(d_, &a, nullptr, &m) == VK_SUCCESS &&
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
      if (vkCreateImage(d_, &c, nullptr, &image) != VK_SUCCESS)
        goto fail;
      VkMemoryRequirements r{};
      vkGetImageMemoryRequirements(d_, image, &r);
      auto n = mem(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      a.allocationSize = r.size;
      a.memoryTypeIndex = n;
      if (n == UINT32_MAX ||
          vkAllocateMemory(d_, &a, nullptr, &im) != VK_SUCCESS ||
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
      if (vkAllocateCommandBuffers(d_, &a, &cb) != VK_SUCCESS)
        goto fail;
      VkCommandBufferBeginInfo begin{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      if (vkBeginCommandBuffer(cb, &begin) != VK_SUCCESS)
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
        vkCmdCopyBufferToImage(cb, staging, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
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
      if (vkEndCommandBuffer(cb) != VK_SUCCESS)
        goto fail;
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cb;
      if (vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
          vkQueueWaitIdle(queue_) != VK_SUCCESS)
        goto fail;
      vkFreeCommandBuffers(d_, pool_, 1, &cb);
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
    vkDestroyImage(d_, image, nullptr);
    vkFreeMemory(d_, im, nullptr);
    vkDestroyBuffer(d_, readback, nullptr);
    vkFreeMemory(d_, rm, nullptr);
    vkDestroyBuffer(d_, staging, nullptr);
    vkFreeMemory(d_, sm, nullptr);
    return DIGITOR_RESULT_OK;
  fail:
    if (image)
      vkDestroyImage(d_, image, nullptr);
    if (im)
      vkFreeMemory(d_, im, nullptr);
    if (readback)
      vkDestroyBuffer(d_, readback, nullptr);
    if (rm)
      vkFreeMemory(d_, rm, nullptr);
    if (staging)
      vkDestroyBuffer(d_, staging, nullptr);
    if (sm)
      vkFreeMemory(d_, sm, nullptr);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult grade_rgba32f(std::span<const Color> src, std::span<Color> out,
                              const ColorGrade &p) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_VULKAN, true, info_.device_name,
                           "checked-in SPIR-V", "grade_spirv.inc:main",
                           "VkComputePipeline:grade-v1");
    if (gpu_failure_point() != GpuFailurePoint::None)
      return injected_failure(gpu_failure_point());
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
          vkDestroyBuffer(d_, bufs[k], nullptr);
        if (memory[k])
          vkFreeMemory(d_, memory[k], nullptr);
      }
    };
    const VkDeviceSize bytes = src.size_bytes();
    for (int k = 0; k < 2; k++) {
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bi.size = bytes;
      bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (vkCreateBuffer(d_, &bi, nullptr, &bufs[k]) != VK_SUCCESS) {
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
      if (vkAllocateMemory(d_, &ai, nullptr, &memory[k]) != VK_SUCCESS ||
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
    if (vkCreateDescriptorSetLayout(d_, &li, nullptr, &layout) != VK_SUCCESS) {
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
    vkCreatePipelineLayout(d_, &pli, nullptr, &pipelineLayout);
#include "gpu/shaders/grade_spirv.inc"
    VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = grade_spv_len;
    si.pCode = reinterpret_cast<const uint32_t *>(grade_spv);
    VkShaderModule shader{};
    vkCreateShaderModule(d_, &si, nullptr, &shader);
    VkComputePipelineCreateInfo ci{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = shader;
    ci.stage.pName = "main";
    ci.layout = pipelineLayout;
    VkPipeline pipeline{};
    VkResult vr = vkCreateComputePipelines(d_, VK_NULL_HANDLE, 1, &ci, nullptr,
                                           &pipeline);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo pi{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    VkDescriptorPool pool{};
    vkCreateDescriptorPool(d_, &pi, nullptr, &pool);
    VkDescriptorSetAllocateInfo di{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    di.descriptorPool = pool;
    di.descriptorSetCount = 1;
    di.pSetLayouts = &layout;
    VkDescriptorSet set{};
    vkAllocateDescriptorSets(d_, &di, &set);
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
    vkUpdateDescriptorSets(d_, 2, writes, 0, nullptr);
    VkCommandBufferAllocateInfo cai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vkAllocateCommandBuffers(d_, &cai, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);
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
    vkCmdDispatch(cmd, (push.n + 63) / 64, 1, 1);
    provenance_.command_recorded = true;
    provenance_.dispatch_or_draw_issued = true;
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (vr == VK_SUCCESS)
      vr = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    if (vr == VK_SUCCESS)
      provenance_.queue_submission_issued = true;
    if (vr == VK_SUCCESS)
      vr = vkQueueWaitIdle(queue_);
    if (vr == VK_SUCCESS)
      provenance_.synchronization_waited = true;
    if (vr == VK_SUCCESS) {
      vkMapMemory(d_, memory[1], 0, bytes, 0, &m);
      std::memcpy(out.data(), m, bytes);
      vkUnmapMemory(d_, memory[1]);
      provenance_.output_written = true;
      provenance_.readback_performed = true;
    }
    vkFreeCommandBuffers(d_, pool_, 1, &cmd);
    vkDestroyDescriptorPool(d_, pool, nullptr);
    vkDestroyPipeline(d_, pipeline, nullptr);
    vkDestroyShaderModule(d_, shader, nullptr);
    vkDestroyPipelineLayout(d_, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(d_, layout, nullptr);
    cleanup();
    provenance_.native_error_code = static_cast<std::int64_t>(vr);
    provenance_.cpu_color_reference_invocations =
      cpu_color_reference_count() - provenance_.cpu_color_reference_invocations;
    return vr == VK_SUCCESS ? DIGITOR_RESULT_OK
                            : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult execute_curves_rgba32f(std::span<const Color> src, std::span<Color> out,
                               const CompiledRgbCurves &curves) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_VULKAN, true, info_.device_name,
                           shader_compiler_.identity().c_str(), "rgb_curves.hlsl:main",
                           "VkComputePipeline:rgb-curves-v1");
    provenance_.curves_enabled=true; provenance_.curve_lut_size=curves.lut_size();
    provenance_.compiled_curve_identity=curves.identity();
    provenance_.native_curve_shader_identity="rgb_curves.hlsl:abi-v1";
    provenance_.native_lut_resource_identity=curves.identity()+":"+info_.device_name;
    if (gpu_failure_point()!=GpuFailurePoint::None) return injected_failure(gpu_failure_point());
    if(src.size()!=out.size())return DIGITOR_RESULT_INVALID_ARGUMENT;if(src.empty())return DIGITOR_RESULT_OK;std::uint32_t pixel_count=0;if(!checked_size_to_uint32(src.size(),pixel_count))return DIGITOR_RESULT_INVALID_ARGUMENT;
    ShaderCompileRequest request{.source=digitor_rgb_curves_hlsl,.entry_point="main",
      .source_name="rgb_curves.hlsl",.target_profile="cs_6_0",.stage=ShaderStage::compute,
      .backend=ShaderBackend::vulkan,.macros={{"DIGITOR_VULKAN","1"}}};
    const auto binary=shader_cache_.get_or_compile(shader_compiler_,request);
    if(!binary){provenance_.failure_stage="SPIR-V generation";return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    provenance_.shader_pipeline_cache=CacheDisposition::Hit;
    VkBuffer buffers[4]{};VkDeviceMemory memories[4]{};VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};VkShaderModule module{};VkPipeline pipeline{};
    VkDescriptorPool descriptor_pool{};VkCommandBuffer command{};
    auto cleanup=[&]{if(command)vkFreeCommandBuffers(d_,pool_,1,&command);if(descriptor_pool)vkDestroyDescriptorPool(d_,descriptor_pool,nullptr);if(pipeline)vkDestroyPipeline(d_,pipeline,nullptr);if(module)vkDestroyShaderModule(d_,module,nullptr);if(pipeline_layout)vkDestroyPipelineLayout(d_,pipeline_layout,nullptr);if(set_layout)vkDestroyDescriptorSetLayout(d_,set_layout,nullptr);for(int i=0;i<4;i++){if(buffers[i])vkDestroyBuffer(d_,buffers[i],nullptr);if(memories[i])vkFreeMemory(d_,memories[i],nullptr);}};
    const auto lut=native_rgb_curves_lut(curves);const auto parameters=native_rgb_curves_parameters(curves,pixel_count);
    const VkDeviceSize sizes[]{src.size_bytes(),out.size_bytes(),lut.size()*sizeof(float),sizeof(parameters)};
    for(int i=0;i<4;i++){VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=sizes[i];bi.usage=i==3?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;if(vkCreateBuffer(d_,&bi,nullptr,&buffers[i])!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkMemoryRequirements req{};vkGetBufferMemoryRequirements(d_,buffers[i],&req);auto type=mem(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;if(type==UINT32_MAX||vkAllocateMemory(d_,&ai,nullptr,&memories[i])!=VK_SUCCESS||vkBindBufferMemory(d_,buffers[i],memories[i],0)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}}
    void*m=nullptr;vkMapMemory(d_,memories[0],0,sizes[0],0,&m);std::memcpy(m,src.data(),sizes[0]);vkUnmapMemory(d_,memories[0]);vkMapMemory(d_,memories[2],0,sizes[2],0,&m);std::memcpy(m,lut.data(),sizes[2]);vkUnmapMemory(d_,memories[2]);vkMapMemory(d_,memories[3],0,sizes[3],0,&m);std::memcpy(m,&parameters,sizes[3]);vkUnmapMemory(d_,memories[3]);provenance_.source_upload_performed=true;
    VkDescriptorSetLayoutBinding bindings[4]{};for(uint32_t i=0;i<4;i++){bindings[i].binding=i;bindings[i].descriptorType=i==3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sli.bindingCount=4;sli.pBindings=bindings;if(vkCreateDescriptorSetLayout(d_,&sli,nullptr,&set_layout)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=1;pli.pSetLayouts=&set_layout;if(vkCreatePipelineLayout(d_,&pli,nullptr,&pipeline_layout)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smi.codeSize=binary.binary.size();smi.pCode=reinterpret_cast<const uint32_t*>(binary.binary.data());if(vkCreateShaderModule(d_,&smi,nullptr,&module)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pci.stage.module=module;pci.stage.pName="main";pci.layout=pipeline_layout;if(vkCreateComputePipelines(d_,VK_NULL_HANDLE,1,&pci,nullptr,&pipeline)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    VkDescriptorPoolSize pool_sizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpi.maxSets=1;dpi.poolSizeCount=2;dpi.pPoolSizes=pool_sizes;if(vkCreateDescriptorPool(d_,&dpi,nullptr,&descriptor_pool)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=descriptor_pool;dai.descriptorSetCount=1;dai.pSetLayouts=&set_layout;VkDescriptorSet set{};if(vkAllocateDescriptorSets(d_,&dai,&set)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkDescriptorBufferInfo infos[4]{};VkWriteDescriptorSet writes[4]{};for(uint32_t i=0;i<4;i++){infos[i]={buffers[i],0,sizes[i]};writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i==3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];}vkUpdateDescriptorSets(d_,4,writes,0,nullptr);provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=true;provenance_.native_lut_cache=CacheDisposition::Miss;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool_;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;if(vkAllocateCommandBuffers(d_,&cai,&command)!=VK_SUCCESS){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(command,&cbi);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,nullptr);vkCmdDispatch(command,(parameters.pixel_count+63)/64,1,1);VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);vkEndCommandBuffer(command);provenance_.command_recorded=provenance_.dispatch_or_draw_issued=true;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;VkResult result=vkQueueSubmit(queue_,1,&submit,VK_NULL_HANDLE);if(result==VK_SUCCESS){provenance_.queue_submission_issued=true;result=vkQueueWaitIdle(queue_);}if(result==VK_SUCCESS){provenance_.synchronization_waited=true;vkMapMemory(d_,memories[1],0,sizes[1],0,&m);std::memcpy(out.data(),m,sizes[1]);vkUnmapMemory(d_,memories[1]);provenance_.output_written=provenance_.readback_performed=provenance_.validation_readback_completed=true;}cleanup();provenance_.native_error_code=result;return result==VK_SUCCESS?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;
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
    if (vkCreateImage(d_, &c, nullptr, &t->x) != VK_SUCCESS) {
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
        vkAllocateMemory(d_, &ai, nullptr, &t->m) != VK_SUCCESS ||
        vkBindImageMemory(d_, t->x, t->m, 0) != VK_SUCCESS) {
      if (t->m)
        vkFreeMemory(d_, t->m, nullptr);
      vkDestroyImage(d_, t->x, nullptr);
      delete t;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t->x;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = c.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(d_, &vi, nullptr, &t->v) != VK_SUCCESS) {
      vkFreeMemory(d_, t->m, nullptr);
      vkDestroyImage(d_, t->x, nullptr);
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
    if (vkCreateBuffer(d_, &c, nullptr, &b->x) != VK_SUCCESS) {
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
        vkAllocateMemory(d_, &ai, nullptr, &b->m) != VK_SUCCESS ||
        vkBindBufferMemory(d_, b->x, b->m, 0) != VK_SUCCESS) {
      if (b->m)
        vkFreeMemory(d_, b->m, nullptr);
      vkDestroyBuffer(d_, b->x, nullptr);
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
      vkDestroyImageView(d_, t->v, nullptr);
      vkDestroyImage(d_, t->x, nullptr);
      vkFreeMemory(d_, t->m, nullptr);
      delete t;
    }
  }
  void destroy_buffer(void *p) noexcept override {
    auto *b = (VkBuf *)p;
    if (b) {
      vkDestroyBuffer(d_, b->x, nullptr);
      vkFreeMemory(d_, b->m, nullptr);
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
