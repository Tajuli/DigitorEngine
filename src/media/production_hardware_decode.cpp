#include "digitor/production_hardware_decode.hpp"

#include <stdexcept>
#include <utility>

namespace digitor {

ProductionHardwareDecodeSession::ProductionHardwareDecodeSession(
    std::unique_ptr<VideoDecoder> decoder,
    ProductionNativeImport importer,
    ProductionHardwareDecodeOptions options)
    : decoder_(std::move(decoder)), importer_(std::move(importer)), options_(options) {
    if (!decoder_) throw std::invalid_argument("hardware decoder is required");
    if (!importer_) throw std::invalid_argument("native GPU importer is required");
    const auto info = decoder_->info();
    qualification_.decoder = info.selected;
    qualification_.handle_type = info.native_handle_type;
    qualification_.status = HardwareDecodeQualificationStatus::not_run;
    if (!info.hardware_accelerated)
      throw std::invalid_argument("production hardware decode requires an accelerated decoder");
}

void ProductionHardwareDecodeSession::fail_qualification(const std::string& diagnostic) noexcept {
  qualification_.status = HardwareDecodeQualificationStatus::failed;
  qualification_.diagnostic = diagnostic;
}

DigitorResult ProductionHardwareDecodeSession::decode(
    FrameNumber frame_number,
    ProductionDecodedFrame& output,
    std::string* diagnostic) noexcept {
  std::scoped_lock lock(mutex_);
  output = {};
  auto fail = [&](DigitorResult result, const std::string& text) {
    fail_qualification(text);
    if (diagnostic) *diagnostic = text;
    return result;
  };

  try {
    auto frame = decoder_->decode(frame_number);
    if (!frame) return fail(DIGITOR_RESULT_INVALID_ARGUMENT, "decoder returned no frame");
    ++qualification_.decoded_frames;
    qualification_.hardware_frame_received =
        qualification_.hardware_frame_received || frame->gpu_resident();

    if (!frame->native_surface) {
      if (options_.require_zero_copy)
        return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                    "hardware frame did not expose a native decoder surface");
      ++qualification_.cpu_readbacks;
      qualification_.cpu_readback_observed = true;
      return fail(DIGITOR_RESULT_UNSUPPORTED,
                  "CPU-resident decode is not accepted by production hardware decode");
    }
    if (frame->cpu_resident()) {
      ++qualification_.cpu_readbacks;
      qualification_.cpu_readback_observed = true;
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "native decode frame also contained CPU pixels");
    }

    const auto& descriptor = frame->native_surface->descriptor();
    qualification_.native_surface_exported = true;
    qualification_.handle_type = descriptor.handle_type;
    if (descriptor.timestamp_us != frame->pts)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "native surface timestamp does not match decoded frame timestamp");
    if (options_.require_monotonic_timestamps && have_timestamp_ && frame->pts < last_timestamp_)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "decoded timestamps moved backwards without a seek");

    ZeroCopyImportRequest request{};
    request.surface = frame->native_surface;
    request.renderer_backend = options_.renderer_backend;
    request.output_format = options_.render_format;
    request.working_color_space = "linear-rgba";

    ProcessedGpuFramePtr imported;
    const auto result = importer_(request, imported);
    if (result != DIGITOR_RESULT_OK)
      return fail(result, "render backend rejected the native decoder surface");
    if (!imported)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "render backend reported success without a GPU frame");

    qualification_.render_backend_imported = true;
    qualification_.timestamp_verified = true;
    // Qualification telemetry is cumulative. A later clean frame must never
    // erase evidence that a CPU readback happened earlier in the session.
    if (!qualification_.cpu_readback_observed && qualification_.cpu_readbacks == 0) {
      qualification_.status = HardwareDecodeQualificationStatus::passed;
      qualification_.diagnostic.clear();
    } else {
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "hardware decode session previously violated zero-copy residency");
    }

    output.number = frame->number;
    output.pts = frame->pts;
    output.duration = frame->duration;
    output.gpu_frame = std::move(imported);
    output.decoder_surface = std::move(frame->native_surface);
    last_timestamp_ = output.pts;
    have_timestamp_ = true;
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return fail(DIGITOR_RESULT_OUT_OF_MEMORY, "hardware decode allocation failed");
  } catch (const std::exception& error) {
    return fail(DIGITOR_RESULT_INTERNAL_ERROR, error.what());
  } catch (...) {
    return fail(DIGITOR_RESULT_INTERNAL_ERROR, "unknown hardware decode failure");
  }
}

DigitorResult ProductionHardwareDecodeSession::seek(
    std::int64_t pts_us,
    std::string* diagnostic) noexcept {
  std::scoped_lock lock(mutex_);
  try {
    decoder_->seek(pts_us);
    have_timestamp_ = false;
    last_timestamp_ = 0;
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::exception& error) {
    if (diagnostic) *diagnostic = error.what();
    return DIGITOR_RESULT_INTERNAL_ERROR;
  } catch (...) {
    if (diagnostic) *diagnostic = "unknown hardware decoder seek failure";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

HardwareDecodeQualification ProductionHardwareDecodeSession::qualification() const {
  std::scoped_lock lock(mutex_);
  // release_verified is evidence, not a getter side effect. It remains false
  // until a platform-specific qualification path records an actual release.
  return qualification_;
}

DecoderInfo ProductionHardwareDecodeSession::decoder_info() const {
  std::scoped_lock lock(mutex_);
  return decoder_->info();
}

}  // namespace digitor
