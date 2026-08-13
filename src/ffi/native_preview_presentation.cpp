#include "digitor/native_preview_presentation.hpp"

#include <utility>

namespace digitor {
namespace {
NativePreviewSubmitResult failure(DigitorResult result, NativePreviewFailure kind,
                                  const char* diagnostic) {
  return {result, kind, diagnostic};
}

bool presentation_format(DigitorPixelFormat format) noexcept {
  return format == DIGITOR_PIXEL_FORMAT_RGBA8_UNORM ||
         format == DIGITOR_PIXEL_FORMAT_BGRA8_UNORM ||
         format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
}
} // namespace

NativePreviewPresentationSession::NativePreviewPresentationSession(
    std::shared_ptr<NativePreviewTextureHost> host) : host_(std::move(host)) {}

NativePreviewPresentationSession::~NativePreviewPresentationSession() {
  std::scoped_lock lock(mutex_);
  destroyed_ = true;
  pending_.reset();
  current_.reset();
  telemetry_.queue_depth = 0;
}

NativePreviewSubmitResult NativePreviewPresentationSession::submit(
    ProcessedGpuFramePtr frame, std::uint64_t generation,
    bool protected_content) noexcept {
  std::scoped_lock lock(mutex_);
  if (destroyed_) return failure(DIGITOR_RESULT_NOT_INITIALIZED,
      NativePreviewFailure::session_destroyed, "presentation session destroyed");
  if (cancelled_) return failure(DIGITOR_RESULT_RESOURCE_IN_USE,
      NativePreviewFailure::cancelled, "presentation cancelled");
  if (!frame) return failure(DIGITOR_RESULT_INVALID_ARGUMENT,
      NativePreviewFailure::null_frame, "timeline result has no GPU frame");
  if (!host_ || !host_->attached()) return failure(DIGITOR_RESULT_NOT_INITIALIZED,
      NativePreviewFailure::missing_flutter_registrar, "Flutter texture registrar is detached");
  if (!frame->context_live()) return failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
      NativePreviewFailure::stale_context, "GPU context is retired");
  if (!frame->ready()) return failure(DIGITOR_RESULT_RESOURCE_IN_USE,
      NativePreviewFailure::missing_synchronization, "producer completion is not ready");
  if (frame->backend() != host_->backend()) return failure(DIGITOR_RESULT_UNSUPPORTED,
      NativePreviewFailure::backend_mismatch, "timeline and Flutter host backends differ");
  if (!frame->has_context_identity(host_->device_identity()))
    return failure(DIGITOR_RESULT_UNSUPPORTED,
        NativePreviewFailure::device_mismatch,
        "timeline and Flutter host device/context identities differ");
  if (!presentation_format(frame->metadata().format)) return failure(DIGITOR_RESULT_UNSUPPORTED,
      NativePreviewFailure::unsupported_pixel_format, "GPU presentation format is unsupported");
  if (frame->metadata().color_metadata.find("linear") != std::string::npos &&
      !host_->deferred_display_transform())
    return failure(DIGITOR_RESULT_UNSUPPORTED,
        NativePreviewFailure::display_transform_unavailable,
        "scene-linear timeline output requires a GPU display transform");
  if (protected_content) return failure(DIGITOR_RESULT_UNSUPPORTED,
      NativePreviewFailure::protected_content_restricted,
      "Flutter host did not qualify protected-content presentation");
  if (generation == 0 || generation <= current_generation_ ||
      generation <= pending_generation_) {
    ++telemetry_.stale_frames_dropped;
    return failure(DIGITOR_RESULT_RESOURCE_IN_USE,
        NativePreviewFailure::stale_generation, "stale or duplicate preview generation");
  }

  ++telemetry_.frames_delivered;
  if (current_) {
    if (pending_) ++telemetry_.queue_replacements;
    pending_ = std::move(frame);
    pending_generation_ = generation;
    telemetry_.queue_depth = 2;
    return {};
  }
  auto result = host_->present(frame, generation);
  if (result != DIGITOR_RESULT_OK)
    return failure(result, NativePreviewFailure::texture_registration_failed,
                   "Flutter host rejected native GPU frame");
  current_ = std::move(frame);
  current_generation_ = generation;
  telemetry_.active_generation = generation;
  telemetry_.queue_depth = 1;
  return {};
}

void NativePreviewPresentationSession::consumed(std::uint64_t generation) noexcept {
  std::scoped_lock lock(mutex_);
  if (!current_ || generation != current_generation_) return;
  ++telemetry_.frames_displayed;
  current_.reset();
  current_generation_ = 0;
  if (pending_ && host_ && host_->attached() &&
      host_->present(pending_, pending_generation_) == DIGITOR_RESULT_OK) {
    current_ = std::move(pending_);
    current_generation_ = pending_generation_;
    telemetry_.active_generation = current_generation_;
  }
  pending_.reset();
  pending_generation_ = 0;
  telemetry_.queue_depth = current_ ? 1 : 0;
}

void NativePreviewPresentationSession::cancel() noexcept {
  std::scoped_lock lock(mutex_);
  cancelled_ = true;
  pending_.reset();
  pending_generation_ = 0;
  telemetry_.queue_depth = current_ ? 1 : 0;
}

NativePreviewTelemetry NativePreviewPresentationSession::telemetry() const noexcept {
  std::scoped_lock lock(mutex_);
  return telemetry_;
}
} // namespace digitor

/* Keep the existing GPU-image additive C ABIs aggregated in the already-
 * qualified native-preview translation unit. The Flutter production C API is
 * compiled as its own translation unit by the root CMake target so its public
 * symbols have a single, explicit owner and cannot be multiply defined. */
#include "gpu_image_session_c_api.cpp"
#include "gpu_image_node_graph_binding_c_api.cpp"
#include "node_graph_c_api_ext.cpp"
