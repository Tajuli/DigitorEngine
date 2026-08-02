#include "digitor/windows_hardware_encode_adapter.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <new>
#include <utility>

namespace digitor {
namespace {

bool codec_supported(ExportCodec codec,
                     const WindowsHardwareEncodeCapabilities& c) noexcept {
  switch (codec) {
    case ExportCodec::h264: return c.h264;
    case ExportCodec::hevc: return c.hevc;
    case ExportCodec::av1: return c.av1;
    case ExportCodec::prores: return false;
  }
  return false;
}

bool container_supported(const std::string& output,
                         const WindowsHardwareEncodeCapabilities& c) {
  auto extension = std::filesystem::path(output).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (extension == ".mp4") return c.mp4;
  if (extension == ".mov") return c.mov;
  if (extension == ".mkv") return c.mkv;
  return false;
}

bool backend_matches(EncoderBackend backend,
                     WindowsHardwareEncoderApi api) noexcept {
  switch (api) {
    case WindowsHardwareEncoderApi::nvenc:
      return backend == EncoderBackend::nvenc;
    case WindowsHardwareEncoderApi::quick_sync:
      return backend == EncoderBackend::quick_sync;
    case WindowsHardwareEncoderApi::media_foundation:
      return backend == EncoderBackend::quick_sync;
  }
  return false;
}

struct AdapterState final {
  std::shared_ptr<const ExportRenderSnapshot> snapshot;
  WindowsHardwareEncoderHost host;
  WindowsHardwareEncodeCapabilities capabilities;
  mutable std::mutex mutex;
  bool opened{};
  bool cancelled{};
};

}  // namespace

ExportContractValidation validate_windows_hardware_encode_contract(
    const ExportRenderSnapshot& snapshot,
    const WindowsHardwareEncodeCapabilities& capabilities) noexcept {
  const auto base = validate_export_snapshot(snapshot);
  if (!base) return base;
  const auto& data = snapshot.data();
  if (!export_policy_uses_gpu(data.policy))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Windows hardware adapter accepts hardware-required snapshots only"};
  if (data.renderer_backend != DIGITOR_RENDERER_D3D12 &&
      data.renderer_backend != DIGITOR_RENDERER_VULKAN)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Windows export requires D3D12 or Vulkan rendering"};
  if (!capabilities.available)
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Windows hardware encoder is unavailable"};
  if (!backend_matches(data.encoder_backend, capabilities.api))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "frozen encoder backend does not match Windows adapter API"};
  if (data.renderer_backend == DIGITOR_RENDERER_D3D12 &&
      capabilities.interop != WindowsGpuInterop::d3d12_native)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "D3D12 export requires native D3D12 encoder interop"};
  if (data.renderer_backend == DIGITOR_RENDERER_VULKAN &&
      capabilities.interop != WindowsGpuInterop::vulkan_external_memory_d3d12)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Vulkan export requires qualified external-memory D3D12 interop"};
  if (!codec_supported(data.profile.codec, capabilities))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "requested codec is unsupported by the Windows hardware encoder"};
  if (data.profile.ten_bit && !capabilities.ten_bit)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "requested ten-bit export is unsupported"};
  if (data.hdr && (!capabilities.ten_bit || !capabilities.hdr_metadata))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "HDR export requires ten-bit surfaces and HDR metadata support"};
  if (data.width > capabilities.max_width ||
      data.height > capabilities.max_height)
    return {DIGITOR_RESULT_UNSUPPORTED,
            "export dimensions exceed Windows encoder capability"};
  if (capabilities.device_identity.empty())
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "Windows adapter device identity is required"};
  if (!container_supported(data.output_path, capabilities))
    return {DIGITOR_RESULT_UNSUPPORTED,
            "output container is unsupported by the Windows adapter"};
  return {DIGITOR_RESULT_OK, "ok"};
}

WindowsHardwareEncodeAdapter create_windows_hardware_encode_adapter(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    WindowsHardwareEncoderHost host) {
  auto state = std::make_shared<AdapterState>();
  state->snapshot = std::move(snapshot);
  state->host = std::move(host);

  WindowsHardwareEncodeAdapter adapter;
  adapter.callbacks.open = [state](const HardwareEncodeConfig& config,
                                   std::string& diagnostic) noexcept {
    try {
      std::scoped_lock lock(state->mutex);
      if (!state->snapshot) {
        diagnostic = "missing immutable export snapshot";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      if (state->opened) {
        diagnostic = "Windows encoder adapter already opened";
        return DIGITOR_RESULT_RESOURCE_IN_USE;
      }
      if (!config.require_hardware || !config.require_zero_copy ||
          !config.require_atomic_finalize ||
          config.backend != state->snapshot->encoder_backend() ||
          config.output_path != state->snapshot->data().output_path) {
        diagnostic = "hardware encode config differs from frozen Windows export contract";
        return DIGITOR_RESULT_UNSUPPORTED;
      }
      if (!state->host.open || !state->host.submit || !state->host.drain ||
          !state->host.finalize_atomic || !state->host.cancel ||
          !state->host.qualification) {
        diagnostic = "Windows hardware encoder host callbacks are incomplete";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      WindowsHardwareEncodeCapabilities capabilities;
      const auto result = state->host.open(config, *state->snapshot,
                                           capabilities, diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
      const auto contract = validate_windows_hardware_encode_contract(
          *state->snapshot, capabilities);
      if (!contract) {
        state->host.cancel();
        diagnostic = contract.diagnostic;
        return contract.result;
      }
      state->capabilities = std::move(capabilities);
      state->opened = true;
      state->cancelled = false;
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory while opening Windows hardware encoder";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      diagnostic = "unexpected Windows hardware encoder open failure";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  };

  adapter.callbacks.submit_gpu_frame =
      [state](const HardwareEncodeFrame& input,
              std::string& diagnostic) noexcept {
    try {
      std::scoped_lock lock(state->mutex);
      if (!state->opened || state->cancelled) {
        diagnostic = "Windows hardware encoder is not active";
        return DIGITOR_RESULT_NOT_INITIALIZED;
      }
      if (!input.frame) {
        diagnostic = "Windows encoder received a null GPU frame";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      const auto frame_contract = validate_frame_against_snapshot(
          *state->snapshot, *input.frame);
      if (!frame_contract) {
        diagnostic = frame_contract.diagnostic;
        return frame_contract.result;
      }
      WindowsHardwareEncodeFrameDescriptor descriptor{
          input.frame, input.pts_us, input.duration_us, input.force_keyframe,
          state->snapshot->data().hdr,
          state->snapshot->data().color_metadata};
      const auto result = state->host.submit(descriptor, diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
      const auto q = state->host.qualification();
      if (!q.no_cpu_readback || !q.no_cpu_staging ||
          !q.synchronization_waited || !q.native_resource_registered) {
        diagnostic = "Windows encoder host violated the zero-copy synchronization contract";
        state->host.cancel();
        state->cancelled = true;
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory while submitting Windows GPU frame";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      diagnostic = "unexpected Windows GPU frame submission failure";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  };

  adapter.callbacks.drain = [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled) {
      diagnostic = "Windows hardware encoder cannot drain";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    return state->host.drain(diagnostic);
  };
  adapter.callbacks.finalize_atomic =
      [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled) {
      diagnostic = "Windows hardware encoder cannot finalize";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    const auto result = state->host.finalize_atomic(diagnostic);
    if (result != DIGITOR_RESULT_OK) return result;
    const auto q = state->host.qualification();
    if (!q.bitstream_produced || !q.atomic_output_finalized ||
        !q.no_cpu_readback || !q.no_cpu_staging) {
      diagnostic = "Windows hardware encoder final qualification failed";
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
    return state->host.qualification ? state->host.qualification()
                                     : WindowsHardwareEncodeQualification{};
  };
  return adapter;
}

}  // namespace digitor
