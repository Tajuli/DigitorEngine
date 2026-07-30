#include "digitor/native_node_platform_factories.hpp"
#include <algorithm>

#if defined(DIGITOR_HAS_VULKAN)
#include <vulkan/vulkan.h>
#endif
#if defined(_WIN32)
#include <d3d12.h>
#endif

namespace digitor {

bool create_vulkan_native_node_pipeline(
    const NativeNodePlatformFactoryContext& ctx,
    const NativeNodeCompiledPipeline& compiled,
    const NativeNodeShaderBinary& binary,
    NativeNodeBackendPipelineHandle& out,
    std::string& diagnostic) noexcept {
  out = {};
#if defined(DIGITOR_HAS_VULKAN)
  if (compiled.backend != DIGITOR_RENDERER_VULKAN ||
      binary.format != NativeNodeBinaryFormat::spirv ||
      !binary.valid_for(compiled) || !ctx.device || binary.bytes.empty() ||
      (binary.bytes.size() % sizeof(std::uint32_t)) != 0) {
    diagnostic = "invalid Vulkan native-node pipeline input";
    return false;
  }
  const auto device = reinterpret_cast<VkDevice>(ctx.device);
  const auto contract = native_node_pipeline_contract(compiled.backend, compiled.kernel);
  VkDescriptorSetLayoutBinding bindings[5]{};
  std::uint32_t descriptor_count = 0;
  for (std::uint32_t i = 0; i < contract.binding_count; ++i) {
    const auto& binding = contract.bindings[i];
    if (binding.kind == NativeNodeBindingKind::constants) continue;
    auto& vk_binding = bindings[descriptor_count++];
    vk_binding.binding = binding.binding;
    vk_binding.descriptorCount = 1;
    vk_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    vk_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  }
  VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dli.bindingCount = descriptor_count;
  dli.pBindings = bindings;
  VkDescriptorSetLayout descriptor_layout{};
  if (vkCreateDescriptorSetLayout(device, &dli, nullptr, &descriptor_layout) != VK_SUCCESS) {
    diagnostic = "vkCreateDescriptorSetLayout failed";
    return false;
  }
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_range.offset = 0;
  push_range.size = contract.constant_bytes;
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &descriptor_layout;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &push_range;
  VkPipelineLayout pipeline_layout{};
  if (vkCreatePipelineLayout(device, &pli, nullptr, &pipeline_layout) != VK_SUCCESS) {
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    diagnostic = "vkCreatePipelineLayout failed";
    return false;
  }
  VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smi.codeSize = binary.bytes.size();
  smi.pCode = reinterpret_cast<const std::uint32_t*>(binary.bytes.data());
  VkShaderModule module{};
  if (vkCreateShaderModule(device, &smi, nullptr, &module) != VK_SUCCESS) {
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    diagnostic = "vkCreateShaderModule failed";
    return false;
  }
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pci.stage.module = module;
  pci.stage.pName = contract.entry_point.data();
  pci.layout = pipeline_layout;
  VkPipeline pipeline{};
  const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
  vkDestroyShaderModule(device, module, nullptr);
  if (result != VK_SUCCESS) {
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    diagnostic = "vkCreateComputePipelines failed";
    return false;
  }
  out.pipeline = reinterpret_cast<std::uintptr_t>(pipeline);
  out.layout = reinterpret_cast<std::uintptr_t>(pipeline_layout);
  out.auxiliary = reinterpret_cast<std::uintptr_t>(descriptor_layout);
  diagnostic.clear();
  return true;
#else
  (void)ctx; (void)compiled; (void)binary;
  diagnostic = "Vulkan support not compiled";
  return false;
#endif
}

void destroy_vulkan_native_node_pipeline(
    const NativeNodePlatformFactoryContext& ctx,
    const NativeNodeBackendPipelineHandle& handle) noexcept {
#if defined(DIGITOR_HAS_VULKAN)
  if (!ctx.device) return;
  const auto device = reinterpret_cast<VkDevice>(ctx.device);
  if (handle.pipeline) vkDestroyPipeline(device, reinterpret_cast<VkPipeline>(handle.pipeline), nullptr);
  if (handle.layout) vkDestroyPipelineLayout(device, reinterpret_cast<VkPipelineLayout>(handle.layout), nullptr);
  if (handle.auxiliary) vkDestroyDescriptorSetLayout(device, reinterpret_cast<VkDescriptorSetLayout>(handle.auxiliary), nullptr);
#else
  (void)ctx; (void)handle;
#endif
}

bool create_d3d12_native_node_pipeline(
    const NativeNodePlatformFactoryContext& ctx,
    const NativeNodeCompiledPipeline& compiled,
    const NativeNodeShaderBinary& binary,
    NativeNodeBackendPipelineHandle& out,
    std::string& diagnostic) noexcept {
  out = {};
#if defined(_WIN32)
  if (compiled.backend != DIGITOR_RENDERER_D3D12 ||
      binary.format != NativeNodeBinaryFormat::dxil ||
      !binary.valid_for(compiled) || !ctx.device || binary.bytes.empty()) {
    diagnostic = "invalid D3D12 native-node pipeline input";
    return false;
  }
  auto* device = reinterpret_cast<ID3D12Device*>(ctx.device);
  const auto contract = native_node_pipeline_contract(compiled.backend, compiled.kernel);
  D3D12_ROOT_PARAMETER params[5]{};
  D3D12_DESCRIPTOR_RANGE ranges[4]{};
  std::uint32_t range_count = 0;
  for (std::uint32_t i = 0; i < contract.binding_count; ++i) {
    const auto& b = contract.bindings[i];
    if (b.kind == NativeNodeBindingKind::constants) {
      params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      params[i].Constants.ShaderRegister = 0;
      params[i].Constants.RegisterSpace = 0;
      params[i].Constants.Num32BitValues = contract.constant_bytes / 4u;
    } else {
      auto& range = ranges[range_count++];
      range.RangeType = b.kind == NativeNodeBindingKind::storage_output
          ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
          : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      range.NumDescriptors = 1;
      range.BaseShaderRegister = b.binding;
      params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      params[i].DescriptorTable.NumDescriptorRanges = 1;
      params[i].DescriptorTable.pDescriptorRanges = &range;
    }
    params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC rsd{};
  rsd.NumParameters = contract.binding_count;
  rsd.pParameters = params;
  ID3DBlob* serialized = nullptr;
  ID3DBlob* errors = nullptr;
  if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors))) {
    if (errors) errors->Release();
    diagnostic = "D3D12SerializeRootSignature failed";
    return false;
  }
  ID3D12RootSignature* root = nullptr;
  HRESULT hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root));
  serialized->Release();
  if (errors) errors->Release();
  if (FAILED(hr)) {
    diagnostic = "CreateRootSignature failed";
    return false;
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = root;
  desc.CS = {binary.bytes.data(), binary.bytes.size()};
  ID3D12PipelineState* pso = nullptr;
  hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr)) {
    root->Release();
    diagnostic = "CreateComputePipelineState failed";
    return false;
  }
  out.pipeline = reinterpret_cast<std::uintptr_t>(pso);
  out.layout = reinterpret_cast<std::uintptr_t>(root);
  diagnostic.clear();
  return true;
#else
  (void)ctx; (void)compiled; (void)binary;
  diagnostic = "D3D12 support not compiled";
  return false;
#endif
}

void destroy_d3d12_native_node_pipeline(
    const NativeNodePlatformFactoryContext&,
    const NativeNodeBackendPipelineHandle& handle) noexcept {
#if defined(_WIN32)
  if (handle.pipeline) reinterpret_cast<ID3D12PipelineState*>(handle.pipeline)->Release();
  if (handle.layout) reinterpret_cast<ID3D12RootSignature*>(handle.layout)->Release();
#else
  (void)handle;
#endif
}

bool record_vulkan_native_node_dispatch(
    const NativeNodePlatformFactoryContext& ctx,
    const NativeNodeBackendPipelineHandle& handle,
    const NativeNodeDispatchGeometry& geometry,
    const NativeNodeDispatchResources& resources,
    std::string& diagnostic) noexcept {
#if defined(DIGITOR_HAS_VULKAN)
  if (!ctx.device || !ctx.command_context || !ctx.descriptor_context ||
      !handle.pipeline || !handle.layout || !handle.auxiliary ||
      geometry.groups_x == 0 || geometry.groups_y == 0 || geometry.groups_z == 0) {
    diagnostic = "invalid Vulkan node dispatch context";
    return false;
  }
  const auto device = reinterpret_cast<VkDevice>(ctx.device);
  const auto command_buffer = reinterpret_cast<VkCommandBuffer>(ctx.command_context);
  const auto descriptor_pool = reinterpret_cast<VkDescriptorPool>(ctx.descriptor_context);
  const auto descriptor_layout = reinterpret_cast<VkDescriptorSetLayout>(handle.auxiliary);
  VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = descriptor_pool;
  allocate_info.descriptorSetCount = 1;
  allocate_info.pSetLayouts = &descriptor_layout;
  VkDescriptorSet descriptor_set{};
  if (vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set) != VK_SUCCESS) {
    diagnostic = "vkAllocateDescriptorSets failed";
    return false;
  }
  VkDescriptorImageInfo image_infos[4]{};
  VkWriteDescriptorSet writes[4]{};
  if (resources.textures.size() > 4) {
    diagnostic = "too many Vulkan node texture bindings";
    return false;
  }
  for (std::size_t i = 0; i < resources.textures.size(); ++i) {
    const auto& texture = resources.textures[i];
    image_infos[i].imageView = reinterpret_cast<VkImageView>(texture.native_texture);
    image_infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = descriptor_set;
    writes[i].dstBinding = texture.slot;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[i].pImageInfo = &image_infos[i];
  }
  vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(resources.textures.size()),
                         writes, 0, nullptr);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    reinterpret_cast<VkPipeline>(handle.pipeline));
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipelineLayout>(handle.layout), 0, 1,
                          &descriptor_set, 0, nullptr);
  if (!resources.constants.empty()) {
    vkCmdPushConstants(command_buffer,
                       reinterpret_cast<VkPipelineLayout>(handle.layout),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       static_cast<std::uint32_t>(resources.constants.size()),
                       resources.constants.data());
  }
  vkCmdDispatch(command_buffer, geometry.groups_x, geometry.groups_y,
                geometry.groups_z);
  diagnostic.clear();
  return true;
#else
  (void)ctx; (void)handle; (void)geometry; (void)resources;
  diagnostic = "Vulkan support not compiled";
  return false;
#endif
}

bool record_d3d12_native_node_dispatch(
    const NativeNodePlatformFactoryContext& ctx,
    const NativeNodeBackendPipelineHandle& handle,
    const NativeNodeDispatchGeometry& geometry,
    const NativeNodeDispatchResources& resources,
    std::string& diagnostic) noexcept {
#if defined(_WIN32)
  if (!ctx.command_context || !ctx.descriptor_context || !handle.pipeline ||
      !handle.layout || geometry.groups_x == 0 || geometry.groups_y == 0 ||
      geometry.groups_z == 0 || resources.constants.empty() ||
      (resources.constants.size() % 4u) != 0) {
    diagnostic = "invalid D3D12 node dispatch context";
    return false;
  }
  auto* command_list =
      reinterpret_cast<ID3D12GraphicsCommandList*>(ctx.command_context);
  auto* heap = reinterpret_cast<ID3D12DescriptorHeap*>(ctx.descriptor_context);
  ID3D12DescriptorHeap* heaps[] = {heap};
  command_list->SetDescriptorHeaps(1, heaps);
  command_list->SetComputeRootSignature(
      reinterpret_cast<ID3D12RootSignature*>(handle.layout));
  command_list->SetPipelineState(
      reinterpret_cast<ID3D12PipelineState*>(handle.pipeline));

  const auto backend = DIGITOR_RENDERER_D3D12;
  const auto contract = native_node_pipeline_contract(backend, resources.kernel);
  if (!validate_native_node_pipeline_contract(contract)) {
    diagnostic = "invalid D3D12 node kernel contract";
    return false;
  }
  for (std::uint32_t root_index = 0; root_index < contract.binding_count;
       ++root_index) {
    const auto& binding = contract.bindings[root_index];
    if (binding.kind == NativeNodeBindingKind::constants) {
      command_list->SetComputeRoot32BitConstants(
          root_index, static_cast<UINT>(resources.constants.size() / 4u),
          resources.constants.data(), 0);
      continue;
    }
    auto it = std::find_if(resources.textures.begin(), resources.textures.end(),
                           [&](const auto& texture) {
                             return texture.slot == binding.binding;
                           });
    if (it == resources.textures.end()) {
      diagnostic = "missing D3D12 descriptor binding";
      return false;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
    gpu_handle.ptr = static_cast<UINT64>(it->native_texture);
    command_list->SetComputeRootDescriptorTable(root_index, gpu_handle);
  }
  command_list->Dispatch(geometry.groups_x, geometry.groups_y,
                         geometry.groups_z);
  diagnostic.clear();
  return true;
#else
  (void)ctx; (void)handle; (void)geometry; (void)resources;
  diagnostic = "D3D12 support not compiled";
  return false;
#endif
}

} // namespace digitor

namespace digitor {

void NativeNodeDescriptorRetirementQueue::retain(
    std::uintptr_t native_handle, std::uint64_t completion_value) {
  if (!native_handle) return;
  pending_.push_back({native_handle, completion_value});
}

std::vector<std::uintptr_t> NativeNodeDescriptorRetirementQueue::collect(
    std::uint64_t completed_value) {
  std::vector<std::uintptr_t> ready;
  auto it = pending_.begin();
  while (it != pending_.end()) {
    if (it->completion_value <= completed_value) {
      ready.push_back(it->native_handle);
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  return ready;
}

bool record_vulkan_native_node_barriers(
    const NativeNodePlatformFactoryContext& ctx,
    const std::vector<NativeNodeTextureTransition>& transitions,
    std::string& diagnostic) noexcept {
#if defined(DIGITOR_HAS_VULKAN)
  if (!ctx.command_context) {
    diagnostic = "missing Vulkan command buffer";
    return false;
  }
  std::vector<VkImageMemoryBarrier> barriers;
  barriers.reserve(transitions.size());
  for (const auto& t : transitions) {
    if (!t.native_resource || t.before == t.after) continue;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = reinterpret_cast<VkImage>(t.native_resource);
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = t.before == NativeNodeAccessState::shader_write
        ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = t.after == NativeNodeAccessState::shader_write
        ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    barriers.push_back(b);
  }
  if (!barriers.empty()) {
    vkCmdPipelineBarrier(reinterpret_cast<VkCommandBuffer>(ctx.command_context),
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<std::uint32_t>(barriers.size()),
                         barriers.data());
  }
  diagnostic.clear();
  return true;
#else
  (void)ctx; (void)transitions;
  diagnostic = "Vulkan support not compiled";
  return false;
#endif
}

bool record_d3d12_native_node_barriers(
    const NativeNodePlatformFactoryContext& ctx,
    const std::vector<NativeNodeTextureTransition>& transitions,
    std::string& diagnostic) noexcept {
#if defined(_WIN32)
  if (!ctx.command_context) {
    diagnostic = "missing D3D12 command list";
    return false;
  }
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.reserve(transitions.size());
  for (const auto& t : transitions) {
    if (!t.native_resource || t.before == t.after) continue;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = reinterpret_cast<ID3D12Resource*>(t.native_resource);
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    const auto state_for = [](NativeNodeAccessState s) {
      switch (s) {
        case NativeNodeAccessState::shader_write:
          return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case NativeNodeAccessState::encoder_read:
          return D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ;
        default:
          return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      }
    };
    b.Transition.StateBefore = state_for(t.before);
    b.Transition.StateAfter = state_for(t.after);
    barriers.push_back(b);
  }
  if (!barriers.empty()) {
    reinterpret_cast<ID3D12GraphicsCommandList*>(ctx.command_context)
        ->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  }
  diagnostic.clear();
  return true;
#else
  (void)ctx; (void)transitions;
  diagnostic = "D3D12 support not compiled";
  return false;
#endif
}

} // namespace digitor
