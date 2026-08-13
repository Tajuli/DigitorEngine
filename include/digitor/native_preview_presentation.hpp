#pragma once

#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace digitor {

enum class NativePreviewFailure : std::uint32_t {
  none,
  null_frame,
  non_gpu_frame,
  backend_mismatch,
  device_mismatch,
  stale_context,
  stale_generation,
  missing_synchronization,
  unsupported_pixel_format,
  display_transform_unavailable,
  protected_content_restricted,
  missing_flutter_registrar,
  texture_registration_failed,
  flutter_engine_detached,
  cancelled,
  session_destroyed
};

struct NativePreviewSubmitResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  NativePreviewFailure failure{NativePreviewFailure::none};
  std::string diagnostic;
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK;
  }
};

struct NativePreviewTelemetry {
  std::uint64_t frames_delivered{};
  std::uint64_t frames_displayed{};
  std::uint64_t stale_frames_dropped{};
  std::uint64_t queue_replacements{};
  std::uint64_t active_generation{};
  std::uint64_t cpu_fallback_frames{};
  std::uint64_t bytes_read_back{};
  std::uint32_t queue_depth{};
};

// Flutter embedding code implements this small interface in a platform-only
// target. The shared_ptr is the exact final timeline frame and is the ownership
// contract; core code never unwraps, maps, copies, or fabricates its handle.
class NativePreviewTextureHost {
 public:
  virtual ~NativePreviewTextureHost() = default;
  [[nodiscard]] virtual bool attached() const noexcept = 0;
  [[nodiscard]] virtual DigitorRendererBackend backend() const noexcept = 0;
  [[nodiscard]] virtual const void* device_identity() const noexcept = 0;
  // Descriptor-driven hosts may apply the display transform after the scene-
  // linear graph result has been accepted by the presentation queue.
  [[nodiscard]] virtual bool deferred_display_transform() const noexcept {
    return false;
  }
  virtual DigitorResult present(const ProcessedGpuFramePtr& frame,
                                std::uint64_t generation) noexcept = 0;
};

class NativePreviewPresentationSession final {
 public:
  explicit NativePreviewPresentationSession(std::shared_ptr<NativePreviewTextureHost> host);
  ~NativePreviewPresentationSession();

  NativePreviewPresentationSession(const NativePreviewPresentationSession&) = delete;
  NativePreviewPresentationSession& operator=(const NativePreviewPresentationSession&) = delete;

  [[nodiscard]] NativePreviewSubmitResult submit(ProcessedGpuFramePtr frame,
                                                  std::uint64_t generation,
                                                  bool protected_content = false) noexcept;
  // Called by the platform host after Flutter's raster consumer has finished
  // with this generation. It advances the one-current/one-pending queue.
  void consumed(std::uint64_t generation) noexcept;
  void cancel() noexcept;
  [[nodiscard]] NativePreviewTelemetry telemetry() const noexcept;

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<NativePreviewTextureHost> host_;
  ProcessedGpuFramePtr current_;
  ProcessedGpuFramePtr pending_;
  std::uint64_t current_generation_{};
  std::uint64_t pending_generation_{};
  NativePreviewTelemetry telemetry_;
  bool cancelled_{};
  bool destroyed_{};
};

} // namespace digitor
