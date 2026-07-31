#pragma once

#include "digitor/windows_zero_copy_qualification.hpp"
#include "digitor/windows_zero_copy_reference_decoder.hpp"

#include <memory>

namespace digitor {

// Binds the real software reference decoder and capability-gated GPU readback
// into the merged qualification runner. Validation readback is never used by
// preview, export, or production playback.
struct WindowsZeroCopyCompleteValidation {
  WindowsZeroCopyReferenceProvider reference;
  WindowsZeroCopyReadback readback;
};

[[nodiscard]] WindowsZeroCopyCompleteValidation make_windows_zero_copy_complete_validation(
    std::shared_ptr<WindowsZeroCopyReferenceDecoder> reference_decoder);

} // namespace digitor
