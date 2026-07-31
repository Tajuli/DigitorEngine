#include "digitor/windows_zero_copy_complete_validation.hpp"

namespace digitor {
WindowsZeroCopyCompleteValidation make_windows_zero_copy_complete_validation(
    std::shared_ptr<WindowsZeroCopyReferenceDecoder> decoder) {
  WindowsZeroCopyCompleteValidation out;
  out.reference=[decoder](std::uint32_t index,std::vector<float>& pixels) noexcept {
    if(!decoder)return DIGITOR_RESULT_INVALID_ARGUMENT;
    WindowsReferenceFrame frame;
    const auto result=decoder->frame(index,frame);
    if(result==DIGITOR_RESULT_OK)pixels=std::move(frame.linear_rgba);
    else pixels.clear();
    return result;
  };
  out.readback=[](const ProcessedGpuFramePtr& frame,std::vector<float>& pixels) noexcept {
    if(!frame){pixels.clear();return DIGITOR_RESULT_INVALID_ARGUMENT;}
    return frame->validation_readback(pixels);
  };
  return out;
}
} // namespace digitor
