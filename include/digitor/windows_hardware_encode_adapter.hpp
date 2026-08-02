#pragma once

#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class WindowsHardwareEncoderApi : std::uint32_t {
  nvenc = 1,
  media_foundation = 2,
  quick_sync = 3,
};

enum class WindowsGpuInterop : std::uint32_t {
  d3d12_native = 1,
  vulkan_external_memory_d3d12 = 2,
};

struct WindowsHardwareEncodeCapabilities final {
  WindowsHardwareEncoderApi api{WindowsHardwareEncoderApi::media_foundation};
  WindowsGpuInterop interop{WindowsGpuInterop::d3d12_native};
  bool available{};
  bool h264{};
  bool hevc{};
  bool av1{};
  bool ten_bit{};
  bool hdr_metadata{};
  bool mp4{};
  bool mov{};
  bool mkv{};
  std::uint32_t max_width{};
  std::uint32_t max_height{};
  std::string device_identity;
  std::string diagnostic;
};

struct WindowsHardwareEncodeFrameDescriptor final {
  ProcessedGpuFramePtr frame;
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  bool force_keyframe{};
  bool hdr{};
  std::string color_metadata;
};

struct WindowsHardwareEncodeQualification final {
  bool adapter_opened{};
  bool gpu_frame_submitted{};
  bool synchronization_waited{};
  bool native_resource_registered{};
  bool bitstream_produced{};
  bool atomic_output_finalized{};
  bool no_cpu_readback{true};
  bool no_cpu_staging{true};
  std::uint64_t submitted_frames{};
  std::uint64_t encoded_frames{};
  std::string diagnostic;
};

// Implemented by the Windows platform layer. Native D3D12/Vulkan, NVENC,
// Media Foundation and oneVPL/QSV handles remain private to the host.
struct WindowsHardwareEncoderHost final {
  std::function<DigitorResult(const HardwareEncodeConfig&,
                              const ExportRenderSnapshot&,
                              WindowsHardwareEncodeCapabilities&,
                              std::string&)> open;
  std::function<DigitorResult(const WindowsHardwareEncodeFrameDescriptor&,
                              std::string&)> submit;
  std::function<DigitorResult(std::string&)> drain;
  std::function<DigitorResult(std::string&)> finalize_atomic;
  std::function<void()> cancel;
  std::function<WindowsHardwareEncodeQualification()> qualification;
};

struct WindowsHardwareEncodeAdapter final {
  HardwareEncoderCallbacks callbacks;
  std::function<WindowsHardwareEncodeQualification()> qualification;
};

[[nodiscard]] ExportContractValidation validate_windows_hardware_encode_contract(
    const ExportRenderSnapshot& snapshot,
    const WindowsHardwareEncodeCapabilities& capabilities) noexcept;

[[nodiscard]] WindowsHardwareEncodeAdapter create_windows_hardware_encode_adapter(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    WindowsHardwareEncoderHost host);

}  // namespace digitor
