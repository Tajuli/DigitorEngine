#pragma once

#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

namespace digitor {

enum class AndroidGpuInterop : std::uint32_t {
  vulkan_ahardwarebuffer = 1,
  gles_eglimage_surface = 2,
};

struct AndroidHardwareEncodeCapabilities final {
  AndroidGpuInterop interop{AndroidGpuInterop::vulkan_ahardwarebuffer};
  bool available{};
  bool hardware_codec{};
  bool input_surface{};
  bool h264{};
  bool hevc{};
  bool ten_bit{};
  bool hdr_metadata{};
  bool mp4{};
  std::uint32_t max_width{};
  std::uint32_t max_height{};
  std::int32_t api_level{};
  std::string codec_name;
  std::string device_identity;
};

struct AndroidHardwareEncodeFrameDescriptor final {
  ProcessedGpuFramePtr frame;
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  bool force_keyframe{};
  bool hdr{};
  std::string color_metadata;
};

struct AndroidHardwareEncodeQualification final {
  bool codec_opened{};
  bool input_surface_created{};
  bool gpu_frame_submitted{};
  bool acquire_sync_waited{};
  bool release_sync_published{};
  bool ahardwarebuffer_or_surface_bound{};
  bool bitstream_produced{};
  bool mp4_finalized{};
  bool no_cpu_readback{true};
  bool no_cpu_staging{true};
  std::uint64_t submitted_frames{};
  std::uint64_t encoded_frames{};
  std::string diagnostic;
};

struct AndroidHardwareEncoderHost final {
  std::function<DigitorResult(const HardwareEncodeConfig&,
                              const ExportRenderSnapshot&,
                              AndroidHardwareEncodeCapabilities&,
                              std::string&)> open;
  std::function<DigitorResult(const AndroidHardwareEncodeFrameDescriptor&,
                              std::string&)> submit;
  std::function<DigitorResult(std::string&)> drain;
  std::function<DigitorResult(std::string&)> finalize_mp4_atomic;
  std::function<void()> cancel;
  std::function<AndroidHardwareEncodeQualification()> qualification;
};

struct AndroidHardwareEncodeAdapter final {
  HardwareEncoderCallbacks callbacks;
  std::function<AndroidHardwareEncodeQualification()> qualification;
};

[[nodiscard]] inline ExportContractValidation
validate_android_hardware_encode_contract(
    const ExportRenderSnapshot& snapshot,
    const AndroidHardwareEncodeCapabilities& capabilities) noexcept {
  const auto base = validate_export_snapshot(snapshot);
  if (!base) return base;
  const auto& data = snapshot.data();
  if (!export_policy_uses_gpu(data.policy))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Android adapter accepts hardware-required snapshots only"};
  if (data.encoder_backend != EncoderBackend::media_codec)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Android export requires MediaCodec encoder backend"};
  if (data.renderer_backend != DIGITOR_RENDERER_VULKAN &&
      data.renderer_backend != DIGITOR_RENDERER_OPENGL_ES)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Android export requires Vulkan or OpenGL ES rendering"};
  if (!capabilities.available || !capabilities.hardware_codec ||
      !capabilities.input_surface)
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "qualified hardware MediaCodec input-surface path is unavailable"};
  if (data.renderer_backend == DIGITOR_RENDERER_VULKAN &&
      capabilities.interop != AndroidGpuInterop::vulkan_ahardwarebuffer)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Vulkan export requires AHardwareBuffer interop"};
  if (data.renderer_backend == DIGITOR_RENDERER_OPENGL_ES &&
      capabilities.interop != AndroidGpuInterop::gles_eglimage_surface)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "GLES export requires EGLImage/input-surface interop"};
  if ((data.profile.codec == ExportCodec::h264 && !capabilities.h264) ||
      (data.profile.codec == ExportCodec::hevc && !capabilities.hevc) ||
      data.profile.codec == ExportCodec::av1 ||
      data.profile.codec == ExportCodec::prores)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "requested codec is unsupported by Android MediaCodec adapter"};
  if (data.profile.ten_bit && !capabilities.ten_bit)
    return {DIGITOR_RESULT_UNSUPPORTED, "ten-bit MediaCodec export unsupported"};
  if (data.hdr && (!capabilities.ten_bit || !capabilities.hdr_metadata))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "HDR export requires ten-bit and HDR metadata support"};
  if (data.width > capabilities.max_width ||
      data.height > capabilities.max_height)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "export dimensions exceed MediaCodec capability"};
  if (capabilities.api_level < 26 || capabilities.codec_name.empty() ||
      capabilities.device_identity.empty())
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "Android codec/API/device identity qualification is incomplete"};
  if (!capabilities.mp4 ||
      std::filesystem::path(data.output_path).extension() != ".mp4")
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Android production export currently requires MP4"};
  return {DIGITOR_RESULT_OK, "ok"};
}

[[nodiscard]] inline AndroidHardwareEncodeAdapter
create_android_hardware_encode_adapter(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    AndroidHardwareEncoderHost host) {
  struct State final {
    std::shared_ptr<const ExportRenderSnapshot> snapshot;
    AndroidHardwareEncoderHost host;
    mutable std::mutex mutex;
    bool opened{};
    bool cancelled{};
  };
  auto state = std::make_shared<State>();
  state->snapshot = std::move(snapshot);
  state->host = std::move(host);

  AndroidHardwareEncodeAdapter adapter;
  adapter.callbacks.open = [state](const HardwareEncodeConfig& config,
                                   std::string& diagnostic) noexcept {
    try {
      std::scoped_lock lock(state->mutex);
      if (!state->snapshot || state->opened) {
        diagnostic = state->opened ? "Android encoder already opened"
                                   : "missing immutable export snapshot";
        return state->opened ? DIGITOR_RESULT_RESOURCE_IN_USE
                             : DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      if (!config.require_hardware || !config.require_zero_copy ||
          !config.require_atomic_finalize ||
          config.backend != EncoderBackend::media_codec ||
          config.output_path != state->snapshot->data().output_path) {
        diagnostic = "MediaCodec config differs from frozen export contract";
        return DIGITOR_RESULT_UNSUPPORTED;
      }
      if (!state->host.open || !state->host.submit || !state->host.drain ||
          !state->host.finalize_mp4_atomic || !state->host.cancel ||
          !state->host.qualification) {
        diagnostic = "Android encoder host callbacks are incomplete";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      AndroidHardwareEncodeCapabilities capabilities;
      auto result = state->host.open(config, *state->snapshot, capabilities,
                                     diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
      const auto contract = validate_android_hardware_encode_contract(
          *state->snapshot, capabilities);
      if (!contract) {
        state->host.cancel();
        diagnostic = contract.diagnostic;
        return contract.result;
      }
      state->opened = true;
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory opening Android MediaCodec encoder";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      diagnostic = "unexpected Android MediaCodec open failure";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  };

  adapter.callbacks.submit_gpu_frame =
      [state](const HardwareEncodeFrame& input,
              std::string& diagnostic) noexcept {
    try {
      std::scoped_lock lock(state->mutex);
      if (!state->opened || state->cancelled || !input.frame) {
        diagnostic = "Android MediaCodec encoder is inactive or frame is null";
        return DIGITOR_RESULT_NOT_INITIALIZED;
      }
      const auto contract = validate_frame_against_snapshot(
          *state->snapshot, *input.frame);
      if (!contract) {
        diagnostic = contract.diagnostic;
        return contract.result;
      }
      AndroidHardwareEncodeFrameDescriptor descriptor{
          input.frame, input.pts_us, input.duration_us, input.force_keyframe,
          state->snapshot->data().hdr,
          state->snapshot->data().color_metadata};
      const auto result = state->host.submit(descriptor, diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
      const auto q = state->host.qualification();
      if (!q.gpu_frame_submitted || !q.acquire_sync_waited ||
          !q.release_sync_published ||
          !q.ahardwarebuffer_or_surface_bound || !q.no_cpu_readback ||
          !q.no_cpu_staging) {
        state->host.cancel();
        state->cancelled = true;
        diagnostic = "Android host violated zero-copy synchronization contract";
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory submitting Android GPU frame";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      diagnostic = "unexpected Android GPU frame submission failure";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  };

  adapter.callbacks.drain = [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled)
      return DIGITOR_RESULT_NOT_INITIALIZED;
    return state->host.drain(diagnostic);
  };
  adapter.callbacks.finalize_atomic =
      [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled)
      return DIGITOR_RESULT_NOT_INITIALIZED;
    const auto result = state->host.finalize_mp4_atomic(diagnostic);
    if (result != DIGITOR_RESULT_OK) return result;
    const auto q = state->host.qualification();
    if (!q.bitstream_produced || !q.mp4_finalized || !q.no_cpu_readback ||
        !q.no_cpu_staging) {
      diagnostic = "Android MediaCodec final qualification failed";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  adapter.callbacks.cancel = [state]() noexcept {
    std::scoped_lock lock(state->mutex);
    if (state->cancelled) return;
    state->cancelled = true;
    if (state->host.cancel) state->host.cancel();
  };
  adapter.qualification = [state]() {
    std::scoped_lock lock(state->mutex);
    return state->host.qualification
               ? state->host.qualification()
               : AndroidHardwareEncodeQualification{};
  };
  return adapter;
}

}  // namespace digitor
