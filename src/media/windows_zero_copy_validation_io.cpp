#include "digitor/windows_zero_copy_validation_io.hpp"

#include <utility>

namespace digitor {

struct WindowsSoftwareReferenceDecoder::Impl { std::string path; };
WindowsSoftwareReferenceDecoder::WindowsSoftwareReferenceDecoder(std::string path)
    : impl_(std::make_unique<Impl>()) { impl_->path=std::move(path); }
WindowsSoftwareReferenceDecoder::~WindowsSoftwareReferenceDecoder()=default;
DigitorResult WindowsSoftwareReferenceDecoder::open(std::string* diagnostic) noexcept {
  if(impl_->path.empty()){if(diagnostic)*diagnostic="reference media path is empty";return DIGITOR_RESULT_INVALID_ARGUMENT;}
#if !defined(DIGITOR_HAS_FFMPEG)
  if(diagnostic)*diagnostic="FFmpeg unavailable";return DIGITOR_RESULT_UNSUPPORTED;
#else
  if(diagnostic)diagnostic->clear();return DIGITOR_RESULT_OK;
#endif
}
DigitorResult WindowsSoftwareReferenceDecoder::frame_rgba32f(
    std::uint32_t, std::vector<float>& out) noexcept {
  out.clear();
  // Intentionally implemented as an integration seam: the qualification host
  // must bind the engine's existing software decoder/color-reference path so
  // the reference is independent from the GPU shader under test.
  return DIGITOR_RESULT_UNSUPPORTED;
}

struct WindowsD3D12ValidationReadback::Impl { void* device{}; };
WindowsD3D12ValidationReadback::WindowsD3D12ValidationReadback(void* device)
    : impl_(std::make_unique<Impl>()) { impl_->device=device; }
WindowsD3D12ValidationReadback::~WindowsD3D12ValidationReadback()=default;
DigitorResult WindowsD3D12ValidationReadback::read(
    const ProcessedGpuFramePtr&, std::vector<float>& out) noexcept {
  out.clear();
  // Qualification-only seam. The D3D12 backend must expose a safe copy-source
  // view of ProcessedGpuFrame; production playback must never call this path.
  return impl_->device ? DIGITOR_RESULT_UNSUPPORTED
                       : DIGITOR_RESULT_INVALID_ARGUMENT;
}

} // namespace digitor
