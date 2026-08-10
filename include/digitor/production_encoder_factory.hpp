#pragma once

#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"

#include <functional>
#include <memory>
#include <string>

namespace digitor {

[[nodiscard]] inline bool hardware_encoder_callbacks_complete(
    const HardwareEncoderCallbacks& callbacks) noexcept {
  return static_cast<bool>(callbacks.open) &&
         static_cast<bool>(callbacks.submit_gpu_frame) &&
         static_cast<bool>(callbacks.drain) &&
         static_cast<bool>(callbacks.finalize_atomic) &&
         static_cast<bool>(callbacks.cancel);
}

struct ProductionEncoderFactoryResult final {
  HardwareEncoderCallbacks callbacks;
  std::function<bool()> zero_copy_qualified;
  bool windows_vulkan_interop_qualified{};
  std::string diagnostic;

  [[nodiscard]] explicit operator bool() const noexcept {
    return hardware_encoder_callbacks_complete(callbacks) &&
           static_cast<bool>(zero_copy_qualified) && diagnostic.empty();
  }
};

using ProductionEncoderFactory = std::function<ProductionEncoderFactoryResult(
    std::shared_ptr<const ExportRenderSnapshot> snapshot)>;

}  // namespace digitor
