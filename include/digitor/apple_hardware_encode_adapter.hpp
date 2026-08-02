#pragma once

#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace digitor {

enum class AppleHardwareCodec : std::uint32_t { h264 = 1, hevc = 2, prores = 3 };
enum class ApplePlatform : std::uint32_t { macos = 1, ios = 2 };

struct AppleHardwareEncodeCapabilities final {
  ApplePlatform platform{ApplePlatform::macos};
  bool available{};
  bool hardware_accelerated{};
  bool iosurface_backed_pool{};
  bool h264{};
  bool hevc{};
  bool prores{};
  bool ten_bit{};
  bool hdr_metadata{};
  bool alpha{};
  bool mp4{};
  bool mov{};
  std::uint32_t max_width{};
  std::uint32_t max_height{};
  std::string device_identity;
};

struct AppleHardwareEncodeFrameDescriptor final {
  ProcessedGpuFramePtr frame;
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  bool force_keyframe{};
  bool hdr{};
  ExportAlphaPolicy alpha_policy{ExportAlphaPolicy::discard};
  std::string color_metadata;
};

struct AppleHardwareEncodeQualification final {
  bool adapter_opened{};
  bool metal_completion_waited{};
  bool iosurface_pixel_buffer_acquired{};
  bool pixel_buffer_pool_reused{};
  bool attachments_propagated{};
  bool bitstream_produced{};
  bool atomic_output_finalized{};
  bool no_cpu_readback{true};
  bool no_cpu_staging{true};
  std::uint64_t submitted_frames{};
  std::uint64_t encoded_frames{};
  std::string diagnostic;
};

struct AppleHardwareEncoderHost final {
  std::function<DigitorResult(const HardwareEncodeConfig&, const ExportRenderSnapshot&,
                              AppleHardwareEncodeCapabilities&, std::string&)> open;
  std::function<DigitorResult(const AppleHardwareEncodeFrameDescriptor&, std::string&)> submit;
  std::function<DigitorResult(std::string&)> drain;
  std::function<DigitorResult(std::string&)> finalize_atomic;
  std::function<void()> cancel;
  std::function<AppleHardwareEncodeQualification()> qualification;
};

struct AppleHardwareEncodeAdapter final {
  HardwareEncoderCallbacks callbacks;
  std::function<AppleHardwareEncodeQualification()> qualification;
};

[[nodiscard]] inline ExportContractValidation validate_apple_hardware_encode_contract(
    const ExportRenderSnapshot& snapshot,
    const AppleHardwareEncodeCapabilities& c) noexcept {
  const auto base = validate_export_snapshot(snapshot);
  if (!base) return base;
  const auto& d = snapshot.data();
  if (d.renderer_backend != DIGITOR_RENDERER_METAL ||
      d.encoder_backend != EncoderBackend::video_toolbox)
    return {DIGITOR_RESULT_UNSUPPORTED, "Apple export requires Metal and VideoToolbox"};
  if (!c.available || !c.hardware_accelerated || !c.iosurface_backed_pool)
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "qualified hardware VideoToolbox IOSurface path unavailable"};
  const bool codec_ok = d.profile.codec == ExportCodec::h264 ? c.h264 :
                        d.profile.codec == ExportCodec::hevc ? c.hevc :
                        d.profile.codec == ExportCodec::prores ? c.prores : false;
  if (!codec_ok) return {DIGITOR_RESULT_UNSUPPORTED, "requested Apple codec unsupported"};
  if (d.profile.ten_bit && !c.ten_bit)
    return {DIGITOR_RESULT_UNSUPPORTED, "ten-bit Apple export unsupported"};
  if (d.hdr && (!c.ten_bit || !c.hdr_metadata))
    return {DIGITOR_RESULT_UNSUPPORTED, "HDR requires ten-bit and metadata support"};
  if (d.alpha_policy != ExportAlphaPolicy::discard && !c.alpha)
    return {DIGITOR_RESULT_UNSUPPORTED, "requested alpha export unsupported"};
  if (d.width > c.max_width || d.height > c.max_height || c.device_identity.empty())
    return {DIGITOR_RESULT_UNSUPPORTED, "Apple device capability mismatch"};
  const auto dot = d.output_path.find_last_of('.');
  const auto ext = dot == std::string::npos ? std::string{} : d.output_path.substr(dot);
  if ((ext == ".mp4" && !c.mp4) || (ext == ".mov" && !c.mov) ||
      (ext != ".mp4" && ext != ".mov"))
    return {DIGITOR_RESULT_UNSUPPORTED, "Apple output container unsupported"};
  return {DIGITOR_RESULT_OK, "ok"};
}

[[nodiscard]] inline AppleHardwareEncodeAdapter create_apple_hardware_encode_adapter(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    AppleHardwareEncoderHost host) {
  struct State final {
    std::shared_ptr<const ExportRenderSnapshot> snapshot;
    AppleHardwareEncoderHost host;
    mutable std::mutex mutex;
    bool opened{};
    bool cancelled{};
  };
  auto state = std::make_shared<State>();
  state->snapshot = std::move(snapshot);
  state->host = std::move(host);
  AppleHardwareEncodeAdapter out;
  out.callbacks.open = [state](const HardwareEncodeConfig& config, std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->snapshot || !state->host.open || !state->host.submit || !state->host.drain ||
        !state->host.finalize_atomic || !state->host.cancel || !state->host.qualification)
      return diagnostic = "incomplete Apple encoder host", DIGITOR_RESULT_INVALID_ARGUMENT;
    if (!config.require_hardware || !config.require_zero_copy ||
        config.backend != EncoderBackend::video_toolbox ||
        config.output_path != state->snapshot->data().output_path)
      return diagnostic = "Apple config differs from frozen export contract", DIGITOR_RESULT_UNSUPPORTED;
    AppleHardwareEncodeCapabilities c;
    const auto r = state->host.open(config, *state->snapshot, c, diagnostic);
    if (r != DIGITOR_RESULT_OK) return r;
    const auto contract = validate_apple_hardware_encode_contract(*state->snapshot, c);
    if (!contract) { state->host.cancel(); diagnostic = contract.diagnostic; return contract.result; }
    state->opened = true;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  out.callbacks.submit_gpu_frame = [state](const HardwareEncodeFrame& input, std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled || !input.frame)
      return diagnostic = "Apple encoder inactive or frame missing", DIGITOR_RESULT_NOT_INITIALIZED;
    const auto contract = validate_frame_against_snapshot(*state->snapshot, *input.frame);
    if (!contract) { diagnostic = contract.diagnostic; return contract.result; }
    const auto& d = state->snapshot->data();
    const auto r = state->host.submit({input.frame, input.pts_us, input.duration_us,
                                       input.force_keyframe, d.hdr, d.alpha_policy,
                                       d.color_metadata}, diagnostic);
    if (r != DIGITOR_RESULT_OK) return r;
    const auto q = state->host.qualification();
    if (!q.metal_completion_waited || !q.iosurface_pixel_buffer_acquired ||
        !q.attachments_propagated || !q.no_cpu_readback || !q.no_cpu_staging) {
      state->host.cancel(); state->cancelled = true;
      diagnostic = "Apple host violated zero-copy synchronization/metadata contract";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  out.callbacks.drain = [state](std::string& d) noexcept {
    std::scoped_lock lock(state->mutex); return state->host.drain(d);
  };
  out.callbacks.finalize_atomic = [state](std::string& d) noexcept {
    std::scoped_lock lock(state->mutex);
    const auto r = state->host.finalize_atomic(d);
    if (r != DIGITOR_RESULT_OK) return r;
    const auto q = state->host.qualification();
    if (!q.bitstream_produced || !q.atomic_output_finalized ||
        !q.no_cpu_readback || !q.no_cpu_staging)
      return d = "Apple final qualification failed", DIGITOR_RESULT_INTERNAL_ERROR;
    d.clear(); return DIGITOR_RESULT_OK;
  };
  out.callbacks.cancel = [state]() noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->cancelled) { state->cancelled = true; if (state->host.cancel) state->host.cancel(); }
  };
  out.qualification = [state]() {
    std::scoped_lock lock(state->mutex);
    return state->host.qualification ? state->host.qualification() : AppleHardwareEncodeQualification{};
  };
  return out;
}

}  // namespace digitor
