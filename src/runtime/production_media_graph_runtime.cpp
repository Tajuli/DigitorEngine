#include "digitor/production_media_graph_runtime.hpp"

#include "core/engine.hpp"

#include <exception>
#include <utility>

namespace digitor {
namespace {

bool encoder_callbacks_complete(const HardwareEncoderCallbacks& callbacks) {
  return static_cast<bool>(callbacks.open) &&
         static_cast<bool>(callbacks.submit_gpu_frame) &&
         static_cast<bool>(callbacks.drain) &&
         static_cast<bool>(callbacks.finalize_atomic) &&
         static_cast<bool>(callbacks.cancel);
}

}  // namespace

ProductionMediaGraphRuntime::ProductionMediaGraphRuntime(
    std::unique_ptr<ProductionHardwareDecodeSession> decoder,
    const ProductionNodeGraph& graph,
    ProductionPreviewPresenter presenter,
    HardwareEncoderCallbacks encoder_callbacks)
    : decoder_(std::move(decoder)),
      graph_(&graph),
      graph_identity_(graph.recipe_identity()),
      presenter_(std::move(presenter)),
      encoder_callbacks_(std::move(encoder_callbacks)) {
  telemetry_.graph_identity = graph_identity_;
}

ProductionMediaGraphRuntime::~ProductionMediaGraphRuntime() { cancel(); }

void ProductionMediaGraphRuntime::set_diagnostic(
    std::string* output, std::string value) noexcept {
  if (!output) return;
  try { *output = std::move(value); } catch (...) { output->clear(); }
}

DigitorResult ProductionMediaGraphRuntime::validate_graph(
    std::string* diagnostic) const noexcept {
  if (!graph_) {
    set_diagnostic(diagnostic, "production node graph is not bound");
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  try {
    if (graph_->recipe_identity() != graph_identity_) {
      set_diagnostic(diagnostic,
          "production node graph changed after binding; create/rebind the production runtime so preview and export use one immutable recipe");
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
  } catch (...) {
    set_diagnostic(diagnostic, "failed to validate production node graph identity");
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult ProductionMediaGraphRuntime::render_frame(
    FrameNumber frame_number, RenderedFrame& output,
    std::string* diagnostic) noexcept {
  if (cancelled_.load(std::memory_order_acquire)) {
    set_diagnostic(diagnostic, "production media graph runtime is cancelled");
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  if (!decoder_) {
    set_diagnostic(diagnostic, "production hardware decoder is not initialized");
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  if (!Engine::instance().is_initialized()) {
    set_diagnostic(diagnostic, "DigitorEngine is not initialized");
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }

  auto result = validate_graph(diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;

  ProductionDecodedFrame decoded;
  std::string decode_diagnostic;
  result = decoder_->decode(frame_number, decoded, &decode_diagnostic);
  if (result != DIGITOR_RESULT_OK) {
    set_diagnostic(diagnostic, decode_diagnostic.empty()
        ? "production hardware decode failed" : std::move(decode_diagnostic));
    return result;
  }
  if (!decoded.gpu_frame || !decoded.gpu_frame->ready()) {
    set_diagnostic(diagnostic,
        "production decoder did not return a ready GPU-resident frame; CPU fallback is forbidden");
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  NativeNodeGraphResult rendered = Engine::instance().execute_native_node_graph(
      *graph_, decoded.gpu_frame, decoded.pts);
  if (rendered.status != NativeNodeGraphStatus::ok ||
      rendered.backend_result != DIGITOR_RESULT_OK || !rendered.frame ||
      !rendered.frame->ready()) {
    set_diagnostic(diagnostic, rendered.message.empty()
        ? "native production node graph failed without CPU fallback"
        : std::move(rendered.message));
    return rendered.backend_result == DIGITOR_RESULT_OK
        ? DIGITOR_RESULT_INTERNAL_ERROR : rendered.backend_result;
  }

  output.frame = std::move(rendered.frame);
  output.pts_us = decoded.pts;
  output.duration_us = decoded.duration;
  return DIGITOR_RESULT_OK;
}

DigitorResult ProductionMediaGraphRuntime::preview(
    FrameNumber frame_number, ProcessedGpuFramePtr* out_frame,
    std::string* diagnostic) noexcept {
  try {
    if (!presenter_) {
      set_diagnostic(diagnostic, "native Flutter preview presenter is not initialized");
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    RenderedFrame rendered;
    const auto result = render_frame(frame_number, rendered, diagnostic);
    if (result != DIGITOR_RESULT_OK) return result;

    std::string present_diagnostic;
    const auto present_result = presenter_(rendered.frame, present_diagnostic);
    if (present_result != DIGITOR_RESULT_OK) {
      set_diagnostic(diagnostic, present_diagnostic.empty()
          ? "native Flutter GPU presentation failed" : std::move(present_diagnostic));
      return present_result;
    }
    if (out_frame) *out_frame = rendered.frame;
    {
      std::lock_guard lock(mutex_);
      ++telemetry_.preview_frames;
      telemetry_.cpu_readbacks = 0;
      telemetry_.cancelled = cancelled_.load(std::memory_order_acquire);
    }
    return DIGITOR_RESULT_OK;
  } catch (const std::exception& error) {
    set_diagnostic(diagnostic, error.what());
    return DIGITOR_RESULT_INTERNAL_ERROR;
  } catch (...) {
    set_diagnostic(diagnostic, "unexpected production preview failure");
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult ProductionMediaGraphRuntime::export_frames(
    std::span<const FrameNumber> frame_numbers, HardwareEncodeConfig config,
    std::string* diagnostic, ProductionExportProgress progress) noexcept {
  try {
    if (frame_numbers.empty()) {
      set_diagnostic(diagnostic, "production export frame range is empty");
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (!encoder_callbacks_complete(encoder_callbacks_)) {
      set_diagnostic(diagnostic, "production hardware encoder callbacks are incomplete");
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    if (config.backend == EncoderBackend::software) {
      set_diagnostic(diagnostic, "software export is forbidden for the strict production GPU path");
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    if (cancelled_.load(std::memory_order_acquire)) {
      set_diagnostic(diagnostic, "production media graph runtime is cancelled");
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }

    auto graph_result = validate_graph(diagnostic);
    if (graph_result != DIGITOR_RESULT_OK) return graph_result;
    config.require_hardware = true;
    config.require_zero_copy = true;
    config.require_monotonic_timestamps = true;
    config.require_atomic_finalize = true;

    ProductionHardwareEncodeSession encoder(config, encoder_callbacks_);
    {
      std::lock_guard lock(mutex_);
      if (active_export_) {
        set_diagnostic(diagnostic, "a production export is already running");
        return DIGITOR_RESULT_RESOURCE_IN_USE;
      }
      active_export_ = &encoder;
      telemetry_.export_running = true;
      telemetry_.cancelled = false;
    }
    const auto clear_active = [this]() noexcept {
      std::lock_guard lock(mutex_);
      active_export_ = nullptr;
      telemetry_.export_running = false;
      telemetry_.cancelled = cancelled_.load(std::memory_order_acquire);
      telemetry_.cpu_readbacks = 0;
    };

    std::string encode_diagnostic;
    auto result = encoder.start(&encode_diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      clear_active();
      set_diagnostic(diagnostic, encode_diagnostic.empty()
          ? "failed to start production hardware encoder" : std::move(encode_diagnostic));
      return result;
    }

    const auto total = static_cast<std::uint64_t>(frame_numbers.size());
    if (progress) progress(0, total);
    std::uint64_t completed = 0;
    for (const FrameNumber frame_number : frame_numbers) {
      if (cancelled_.load(std::memory_order_acquire)) {
        encoder.cancel();
        clear_active();
        set_diagnostic(diagnostic, "production export cancelled");
        return DIGITOR_RESULT_RESOURCE_IN_USE;
      }
      RenderedFrame rendered;
      result = render_frame(frame_number, rendered, diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        encoder.cancel(); clear_active(); return result;
      }
      HardwareEncodeFrame encode_frame;
      encode_frame.frame = std::move(rendered.frame);
      encode_frame.pts_us = rendered.pts_us;
      encode_frame.duration_us = rendered.duration_us;
      result = encoder.submit(std::move(encode_frame), &encode_diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        encoder.cancel(); clear_active();
        set_diagnostic(diagnostic, encode_diagnostic.empty()
            ? "production GPU frame submission failed" : std::move(encode_diagnostic));
        return result;
      }
      {
        std::lock_guard lock(mutex_);
        ++telemetry_.export_frames;
      }
      ++completed;
      if (progress) progress(completed, total);
    }

    result = encoder.finish(&encode_diagnostic);
    clear_active();
    if (result != DIGITOR_RESULT_OK) {
      set_diagnostic(diagnostic, encode_diagnostic.empty()
          ? "production hardware export finalize failed" : std::move(encode_diagnostic));
      return result;
    }
    return DIGITOR_RESULT_OK;
  } catch (const std::exception& error) {
    { std::lock_guard lock(mutex_); active_export_ = nullptr; telemetry_.export_running = false; }
    set_diagnostic(diagnostic, error.what());
    return DIGITOR_RESULT_INTERNAL_ERROR;
  } catch (...) {
    { std::lock_guard lock(mutex_); active_export_ = nullptr; telemetry_.export_running = false; }
    set_diagnostic(diagnostic, "unexpected production export failure");
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

void ProductionMediaGraphRuntime::cancel() noexcept {
  cancelled_.store(true, std::memory_order_release);
  std::lock_guard lock(mutex_);
  telemetry_.cancelled = true;
  if (active_export_) active_export_->cancel();
}

ProductionMediaGraphRuntimeTelemetry ProductionMediaGraphRuntime::telemetry() const {
  std::lock_guard lock(mutex_);
  auto value = telemetry_;
  value.cancelled = cancelled_.load(std::memory_order_acquire);
  value.graph_identity = graph_identity_;
  return value;
}

}  // namespace digitor
