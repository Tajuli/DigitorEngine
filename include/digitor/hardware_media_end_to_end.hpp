#pragma once

#include "digitor/production_hardware_decode.hpp"
#include "digitor/production_hardware_encode.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace digitor {

using HardwareMediaGpuProcess = std::function<DigitorResult(
    const ProductionDecodedFrame&, ProcessedGpuFramePtr&, std::string&)>;

struct HardwareMediaEndToEndQualification final {
  std::uint64_t decoded_frames{};
  std::uint64_t processed_frames{};
  std::uint64_t encoded_frames{};
  bool native_decoder_surface_retained{};
  bool decode_zero_copy{};
  bool gpu_processing_completed{};
  bool renderer_backend_continuity{};
  bool timestamp_parity{};
  bool encode_zero_copy{};
  bool completed{};
  std::string diagnostic;

  [[nodiscard]] bool passed() const noexcept {
    return completed && decoded_frames > 0 && decoded_frames == processed_frames &&
           processed_frames == encoded_frames && native_decoder_surface_retained &&
           decode_zero_copy && gpu_processing_completed &&
           renderer_backend_continuity && timestamp_parity && encode_zero_copy &&
           diagnostic.empty();
  }
};

class HardwareMediaEndToEndSession final {
 public:
  HardwareMediaEndToEndSession(ProductionHardwareDecodeSession& decoder,
                               ProductionHardwareEncodeSession& encoder,
                               HardwareMediaGpuProcess process)
      : decoder_(decoder), encoder_(encoder), process_(std::move(process)) {
    if (!process_) throw std::invalid_argument("GPU media process callback is required");
  }

  HardwareMediaEndToEndSession(const HardwareMediaEndToEndSession&) = delete;
  HardwareMediaEndToEndSession& operator=(const HardwareMediaEndToEndSession&) = delete;

  [[nodiscard]] DigitorResult start(std::string* diagnostic = nullptr) noexcept {
    if (started_ || finished_) return fail(DIGITOR_RESULT_RESOURCE_IN_USE,
                                           "end-to-end hardware media session already started",
                                           diagnostic);
    std::string local;
    const auto result = encoder_.start(&local);
    if (result != DIGITOR_RESULT_OK)
      return fail(result, local.empty() ? "hardware encoder start failed" : local,
                  diagnostic);
    started_ = true;
    qualification_.diagnostic.clear();
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  }

  [[nodiscard]] DigitorResult process_frame(FrameNumber frame_number,
                                            bool force_keyframe = false,
                                            std::string* diagnostic = nullptr) noexcept {
    if (!started_ || finished_)
      return fail(DIGITOR_RESULT_NOT_INITIALIZED,
                  "end-to-end hardware media session is not running", diagnostic);

    ProductionDecodedFrame decoded;
    std::string local;
    auto result = decoder_.decode(frame_number, decoded, &local);
    if (result != DIGITOR_RESULT_OK)
      return fail(result, local.empty() ? "hardware decode failed" : local, diagnostic);

    const auto decode_q = decoder_.qualification();
    ++qualification_.decoded_frames;
    qualification_.native_decoder_surface_retained =
        qualification_.native_decoder_surface_retained ||
        static_cast<bool>(decoded.decoder_surface);
    qualification_.decode_zero_copy =
        decode_q.status == HardwareDecodeQualificationStatus::passed &&
        decode_q.hardware_frame_received && decode_q.native_surface_exported &&
        decode_q.render_backend_imported && !decode_q.cpu_readback_observed &&
        decode_q.cpu_readbacks == 0;
    if (!qualification_.decode_zero_copy || !decoded.gpu_frame ||
        !decoded.decoder_surface)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "decode/import path did not preserve the zero-copy contract", diagnostic);

    ProcessedGpuFramePtr processed;
    local.clear();
    result = process_(decoded, processed, local);
    if (result != DIGITOR_RESULT_OK)
      return fail(result, local.empty() ? "GPU processing failed" : local, diagnostic);
    if (!processed || !processed->ready() || !processed->context_live() ||
        processed->backend() == DIGITOR_RENDERER_CPU)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "GPU processing returned an invalid or CPU frame", diagnostic);

    ++qualification_.processed_frames;
    qualification_.gpu_processing_completed = true;
    qualification_.renderer_backend_continuity =
        decoded.gpu_frame->backend() == processed->backend();
    qualification_.timestamp_parity =
        decoded.gpu_frame->metadata().timestamp == decoded.pts &&
        processed->metadata().timestamp == decoded.pts;
    if (!qualification_.renderer_backend_continuity)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "renderer backend changed between native import and GPU processing",
                  diagnostic);
    if (!qualification_.timestamp_parity)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "decode/import/process timestamp parity failed", diagnostic);

    local.clear();
    result = encoder_.submit(
        {processed, decoded.pts, decoded.duration, force_keyframe}, &local);
    if (result != DIGITOR_RESULT_OK)
      return fail(result, local.empty() ? "hardware encode submission failed" : local,
                  diagnostic);

    const auto encode_q = encoder_.telemetry();
    qualification_.encoded_frames = encode_q.accepted_frames;
    qualification_.encode_zero_copy = encode_q.cpu_readbacks == 0 &&
                                      encode_q.rejected_frames == 0;
    if (!qualification_.encode_zero_copy)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "hardware encoder reported a CPU readback or rejected frame", diagnostic);

    qualification_.diagnostic.clear();
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  }

  [[nodiscard]] DigitorResult finish(std::string* diagnostic = nullptr) noexcept {
    if (!started_ || finished_)
      return fail(DIGITOR_RESULT_NOT_INITIALIZED,
                  "end-to-end hardware media session cannot finish", diagnostic);
    std::string local;
    const auto result = encoder_.finish(&local);
    if (result != DIGITOR_RESULT_OK)
      return fail(result, local.empty() ? "hardware encoder finish failed" : local,
                  diagnostic);

    const auto encode_q = encoder_.telemetry();
    qualification_.encoded_frames = encode_q.accepted_frames;
    qualification_.encode_zero_copy = encode_q.cpu_readbacks == 0 &&
                                      encode_q.rejected_frames == 0;
    qualification_.completed =
        encode_q.state == HardwareEncodeState::completed &&
        qualification_.encoded_frames == qualification_.processed_frames;
    finished_ = true;
    if (!qualification_.passed())
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "end-to-end hardware media qualification failed", diagnostic);
    qualification_.diagnostic.clear();
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  }

  [[nodiscard]] const HardwareMediaEndToEndQualification& qualification() const noexcept {
    return qualification_;
  }

 private:
  DigitorResult fail(DigitorResult result, std::string text,
                     std::string* diagnostic) noexcept {
    qualification_.completed = false;
    qualification_.diagnostic = std::move(text);
    if (diagnostic) *diagnostic = qualification_.diagnostic;
    return result;
  }

  ProductionHardwareDecodeSession& decoder_;
  ProductionHardwareEncodeSession& encoder_;
  HardwareMediaGpuProcess process_;
  HardwareMediaEndToEndQualification qualification_{};
  bool started_{};
  bool finished_{};
};

}  // namespace digitor
