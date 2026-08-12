#pragma once
#include "digitor/gpu_frame.hpp"
#include <atomic>
#include <cstdint>
#include <string>
namespace digitor {
enum class GpuSourceOrigin { CpuUpload, Decoder, ProcessedFrame, RenderGraphIntermediate };
enum class GpuPrecisionMode { Float32, Float16 };
enum class GpuReadiness { UploadRequired, Ready, Pending };
struct GpuSourceResource final {
  GpuSourceOrigin origin{GpuSourceOrigin::ProcessedFrame};
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  std::uint64_t context_identity{};
  std::uint32_t width{},height{};
  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
  GpuPrecisionMode precision{GpuPrecisionMode::Float32};
  std::string color_metadata_identity{"linear-rgba"};
  std::string source_identity;
  GpuReadiness readiness{GpuReadiness::Pending};
  // Decoder/upload integrations retain their native resource through this
  // internal ownership token and publish completion through ready_token.
  std::shared_ptr<void> ownership_token;
  std::shared_ptr<std::atomic_bool> ready_token;
  ProcessedGpuFramePtr frame;
  [[nodiscard]] bool usable_by(DigitorRendererBackend b,std::uint64_t c)const noexcept{
    const bool ready_now=frame?frame->ready():(ownership_token&&ready_token&&ready_token->load(std::memory_order_acquire));
    const bool float_rgba = format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
                            format == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;
    if(!ready_now||readiness!=GpuReadiness::Ready||backend!=b||context_identity!=c||!width||!height||!float_rgba||precision!=GpuPrecisionMode::Float32||color_metadata_identity.empty())return false;
    if(frame){const auto&m=frame->metadata();return frame->backend()==backend&&m.width==width&&m.height==height&&m.format==format&&m.color_metadata==color_metadata_identity;}
    return true;
  }
};
}
