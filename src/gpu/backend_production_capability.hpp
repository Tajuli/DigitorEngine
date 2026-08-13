#pragma once

#include "digitor/digitor.h"
#include "digitor/native_media.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace digitor {

struct D3D12ProductionResources { void* device{}; void* command_queue{}; };
struct VulkanProductionResources {
  void* instance{}; void* physical_device{}; void* device{}; void* queue{};
  std::uint32_t queue_family{};
};
struct GlesProductionResources { void* egl_display{}; void* egl_context{}; };
struct MetalProductionResources { void* device{}; };

using BackendProductionResources = std::variant<std::monostate,
    D3D12ProductionResources, VulkanProductionResources,
    GlesProductionResources, MetalProductionResources>;

using NativePreviewDescriptorCallback = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, std::uint64_t,
    DigitorNativeGpuTextureDescriptor&, std::string&)>;

struct BackendProductionCapability {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  std::uint64_t context_identity{};
  // Exact ProcessedGpuFrame context pointer for the initialized backend generation.
  // Engine fills this from the selected IRenderBackend instance before provider install.
  const void* frame_context_identity{};
  BackendProductionResources resources;
  // Renderer-owned native media import. This callback is created by the exact
  // selected backend generation so imported frames carry real backend context
  // identity/lifetime instead of a platform-fabricated GPU token.
  NativeMediaImportCallback native_media_import{};
  // Renderer-owned final preview descriptor builder. The selected backend
  // remains the only code allowed to unwrap its ProcessedGpuFrame ownership
  // and publish a Flutter-compatible native GPU resource.
  NativePreviewDescriptorCallback native_preview_descriptor{};
  DigitorNativePreviewCapabilities native_preview_capabilities{};
  [[nodiscard]] bool valid() const noexcept {
    return backend != DIGITOR_RENDERER_CPU && context_identity != 0 &&
           frame_context_identity != nullptr && resources.index() != 0;
  }
};

}  // namespace digitor
