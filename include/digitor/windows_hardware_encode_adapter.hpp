#pragma once

#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

namespace digitor {

enum class WindowsHardwareEncoderApi : std::uint32_t { nvenc = 1, media_foundation = 2, quick_sync = 3 };
enum class WindowsGpuInterop : std::uint32_t { d3d12_native = 1, vulkan_external_memory_d3d12 = 2 };

struct WindowsHardwareEncodeCapabilities final {
  WindowsHardwareEncoderApi api{WindowsHardwareEncoderApi::media_foundation};
  WindowsGpuInterop interop{WindowsGpuInterop::d3d12_native};
  bool available{}, h264{}, hevc{}, av1{}, ten_bit{}, hdr_metadata{};
  bool mp4{}, mov{}, mkv{};
  std::uint32_t max_width{}, max_height{};
  std::string device_identity;
  std::string diagnostic;
};

struct WindowsHardwareEncodeFrameDescriptor final {
  ProcessedGpuFramePtr frame;
  std::int64_t pts_us{}, duration_us{};
  bool force_keyframe{}, hdr{};
  std::string color_metadata;
};

struct WindowsHardwareEncodeQualification final {
  bool adapter_opened{}, gpu_frame_submitted{}, synchronization_waited{};
  bool native_resource_registered{}, bitstream_produced{}, atomic_output_finalized{};
  bool no_cpu_readback{true}, no_cpu_staging{true};
  std::uint64_t submitted_frames{}, encoded_frames{};
  std::string diagnostic;
};

struct WindowsHardwareEncoderHost final {
  std::function<DigitorResult(const HardwareEncodeConfig&, const ExportRenderSnapshot&,
                              WindowsHardwareEncodeCapabilities&, std::string&)> open;
  std::function<DigitorResult(const WindowsHardwareEncodeFrameDescriptor&, std::string&)> submit;
  std::function<DigitorResult(std::string&)> drain;
  std::function<DigitorResult(std::string&)> finalize_atomic;
  std::function<void()> cancel;
  std::function<WindowsHardwareEncodeQualification()> qualification;
};

struct WindowsHardwareEncodeAdapter final {
  HardwareEncoderCallbacks callbacks;
  std::function<WindowsHardwareEncodeQualification()> qualification;
};

namespace windows_encode_detail {
inline bool codec_supported(ExportCodec codec, const WindowsHardwareEncodeCapabilities& c) noexcept {
  switch (codec) {
    case ExportCodec::h264: return c.h264;
    case ExportCodec::hevc: return c.hevc;
    case ExportCodec::av1: return c.av1;
    case ExportCodec::prores: return false;
  }
  return false;
}
inline bool backend_matches(EncoderBackend backend, WindowsHardwareEncoderApi api) noexcept {
  if (api == WindowsHardwareEncoderApi::nvenc) return backend == EncoderBackend::nvenc;
  return backend == EncoderBackend::quick_sync;
}
inline std::string extension(std::string path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  path = path.substr(dot);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return path;
}
inline bool container_supported(const std::string& path, const WindowsHardwareEncodeCapabilities& c) {
  const auto ext = extension(path);
  return (ext == ".mp4" && c.mp4) || (ext == ".mov" && c.mov) || (ext == ".mkv" && c.mkv);
}
struct State final {
  std::shared_ptr<const ExportRenderSnapshot> snapshot;
  WindowsHardwareEncoderHost host;
  WindowsHardwareEncodeCapabilities capabilities;
  mutable std::mutex mutex;
  bool opened{}, cancelled{};
};
}  // namespace windows_encode_detail

[[nodiscard]] inline ExportContractValidation validate_windows_hardware_encode_contract(
    const ExportRenderSnapshot& snapshot,
    const WindowsHardwareEncodeCapabilities& c) noexcept {
  const auto base = validate_export_snapshot(snapshot);
  if (!base) return base;
  const auto& d = snapshot.data();
  if (!export_policy_uses_gpu(d.policy)) return {DIGITOR_RESULT_UNSUPPORTED, "Windows adapter requires hardware export"};
  if (d.renderer_backend != DIGITOR_RENDERER_D3D12 && d.renderer_backend != DIGITOR_RENDERER_VULKAN)
    return {DIGITOR_RESULT_UNSUPPORTED, "Windows export requires D3D12 or Vulkan"};
  if (!c.available) return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "Windows hardware encoder unavailable"};
  if (!windows_encode_detail::backend_matches(d.encoder_backend, c.api))
    return {DIGITOR_RESULT_UNSUPPORTED, "encoder backend does not match Windows adapter"};
  if (d.renderer_backend == DIGITOR_RENDERER_D3D12 && c.interop != WindowsGpuInterop::d3d12_native)
    return {DIGITOR_RESULT_UNSUPPORTED, "D3D12 export requires native D3D12 interop"};
  if (d.renderer_backend == DIGITOR_RENDERER_VULKAN && c.interop != WindowsGpuInterop::vulkan_external_memory_d3d12)
    return {DIGITOR_RESULT_UNSUPPORTED, "Vulkan export requires external-memory D3D12 interop"};
  if (!windows_encode_detail::codec_supported(d.profile.codec, c))
    return {DIGITOR_RESULT_UNSUPPORTED, "requested codec unsupported"};
  if (d.profile.ten_bit && !c.ten_bit) return {DIGITOR_RESULT_UNSUPPORTED, "ten-bit export unsupported"};
  if (d.hdr && (!c.ten_bit || !c.hdr_metadata)) return {DIGITOR_RESULT_UNSUPPORTED, "HDR metadata path unsupported"};
  if (d.width > c.max_width || d.height > c.max_height)
    return {DIGITOR_RESULT_UNSUPPORTED, "dimensions exceed encoder capability"};
  if (c.device_identity.empty()) return {DIGITOR_RESULT_INVALID_ARGUMENT, "device identity required"};
  if (!windows_encode_detail::container_supported(d.output_path, c))
    return {DIGITOR_RESULT_UNSUPPORTED, "output container unsupported"};
  return {DIGITOR_RESULT_OK, "ok"};
}

[[nodiscard]] inline WindowsHardwareEncodeAdapter create_windows_hardware_encode_adapter(
    std::shared_ptr<const ExportRenderSnapshot> snapshot, WindowsHardwareEncoderHost host) {
  using windows_encode_detail::State;
  auto state = std::make_shared<State>();
  state->snapshot = std::move(snapshot);
  state->host = std::move(host);
  WindowsHardwareEncodeAdapter out;
  out.callbacks.open = [state](const HardwareEncodeConfig& config, std::string& diagnostic) noexcept {
    try {
      std::scoped_lock lock(state->mutex);
      if (!state->snapshot) { diagnostic = "missing immutable snapshot"; return DIGITOR_RESULT_INVALID_ARGUMENT; }
      if (state->opened) { diagnostic = "Windows adapter already opened"; return DIGITOR_RESULT_RESOURCE_IN_USE; }
      if (!config.require_hardware || !config.require_zero_copy || !config.require_atomic_finalize ||
          config.backend != state->snapshot->encoder_backend() ||
          config.output_path != state->snapshot->data().output_path) {
        diagnostic = "encode config differs from frozen Windows contract";
        return DIGITOR_RESULT_UNSUPPORTED;
      }
      if (!state->host.open || !state->host.submit || !state->host.drain ||
          !state->host.finalize_atomic || !state->host.cancel || !state->host.qualification) {
        diagnostic = "Windows host callbacks incomplete";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      WindowsHardwareEncodeCapabilities caps;
      const auto result = state->host.open(config, *state->snapshot, caps, diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
      const auto contract = validate_windows_hardware_encode_contract(*state->snapshot, caps);
      if (!contract) { state->host.cancel(); diagnostic = contract.diagnostic; return contract.result; }
      state->capabilities = std::move(caps); state->opened = true; state->cancelled = false;
      diagnostic.clear(); return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) { diagnostic = "Windows encoder open OOM"; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch (...) { diagnostic = "Windows encoder open failure"; return DIGITOR_RESULT_INTERNAL_ERROR; }
  };
  out.callbacks.submit_gpu_frame = [state](const HardwareEncodeFrame& input, std::string& diagnostic) noexcept {
    try {
      std::scoped_lock lock(state->mutex);
      if (!state->opened || state->cancelled) { diagnostic = "Windows adapter inactive"; return DIGITOR_RESULT_NOT_INITIALIZED; }
      if (!input.frame) { diagnostic = "null GPU frame"; return DIGITOR_RESULT_INVALID_ARGUMENT; }
      const auto contract = validate_frame_against_snapshot(*state->snapshot, *input.frame);
      if (!contract) { diagnostic = contract.diagnostic; return contract.result; }
      WindowsHardwareEncodeFrameDescriptor descriptor{input.frame, input.pts_us, input.duration_us,
                                                       input.force_keyframe, state->snapshot->data().hdr,
                                                       state->snapshot->data().color_metadata};
      const auto result = state->host.submit(descriptor, diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
      const auto q = state->host.qualification();
      if (!q.no_cpu_readback || !q.no_cpu_staging || !q.synchronization_waited || !q.native_resource_registered) {
        state->host.cancel(); state->cancelled = true;
        diagnostic = "Windows host violated zero-copy synchronization contract";
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }
      diagnostic.clear(); return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) { diagnostic = "Windows frame submit OOM"; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch (...) { diagnostic = "Windows frame submit failure"; return DIGITOR_RESULT_INTERNAL_ERROR; }
  };
  out.callbacks.drain = [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled) { diagnostic = "Windows adapter cannot drain"; return DIGITOR_RESULT_NOT_INITIALIZED; }
    return state->host.drain(diagnostic);
  };
  out.callbacks.finalize_atomic = [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled) { diagnostic = "Windows adapter cannot finalize"; return DIGITOR_RESULT_NOT_INITIALIZED; }
    const auto result = state->host.finalize_atomic(diagnostic);
    if (result != DIGITOR_RESULT_OK) return result;
    const auto q = state->host.qualification();
    if (!q.bitstream_produced || !q.atomic_output_finalized || !q.no_cpu_readback || !q.no_cpu_staging) {
      diagnostic = "Windows encoder final qualification failed";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    diagnostic.clear(); return DIGITOR_RESULT_OK;
  };
  out.callbacks.cancel = [state]() noexcept {
    std::scoped_lock lock(state->mutex);
    if (state->cancelled) return;
    state->cancelled = true;
    if (state->host.cancel) state->host.cancel();
  };
  out.qualification = [state]() {
    std::scoped_lock lock(state->mutex);
    return state->host.qualification ? state->host.qualification() : WindowsHardwareEncodeQualification{};
  };
  return out;
}

}  // namespace digitor
