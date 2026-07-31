#pragma once

#include "digitor/gpu_frame.hpp"
#include "digitor/windows_zero_copy_runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class WindowsNativeConsumerKind : std::uint32_t {
  preview_swapchain = 1,
  hardware_encoder = 2,
};

struct WindowsD3D12FrameLease {
  void* resource{};                 // ID3D12Resource*, borrowed for lease duration
  void* producer_fence{};           // ID3D12Fence*, optional
  std::uint64_t producer_fence_value{};
  std::uint32_t width{};
  std::uint32_t height{};
  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  WindowsNativeConsumerKind consumer{};
  std::function<void()> release;

  WindowsD3D12FrameLease() = default;
  WindowsD3D12FrameLease(const WindowsD3D12FrameLease&) = delete;
  WindowsD3D12FrameLease& operator=(const WindowsD3D12FrameLease&) = delete;
  WindowsD3D12FrameLease(WindowsD3D12FrameLease&& other) noexcept;
  WindowsD3D12FrameLease& operator=(WindowsD3D12FrameLease&& other) noexcept;
  ~WindowsD3D12FrameLease();
  void reset() noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return resource != nullptr; }
};

using WindowsD3D12LeaseProvider = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, WindowsNativeConsumerKind,
    WindowsD3D12FrameLease&)>;

struct WindowsPreviewConsumerConfig {
  void* command_queue{};            // ID3D12CommandQueue*
  void* swapchain{};                // IDXGISwapChain3*
  std::uint32_t back_buffer_count{3};
  bool preserve_hdr_linear_values{true};
};

struct WindowsEncoderConsumerConfig {
  std::string codec{"hevc"};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps_num{30};
  std::uint32_t fps_den{1};
  std::uint32_t bit_depth{10};
  bool require_hardware_encoder{true};
  bool require_zero_copy{true};
};

struct WindowsNativeConsumerTelemetry {
  std::uint64_t preview_frames{};
  std::uint64_t encoder_frames{};
  std::uint64_t lease_failures{};
  std::uint64_t synchronization_failures{};
  std::uint64_t timestamp_mismatches{};
  std::uint64_t identity_mismatches{};
  std::uint64_t cpu_copies{};
  std::string diagnostic;
};

class WindowsD3D12PreviewConsumer final {
public:
  WindowsD3D12PreviewConsumer(WindowsPreviewConsumerConfig,
                              WindowsD3D12LeaseProvider);
  ~WindowsD3D12PreviewConsumer();
  [[nodiscard]] DigitorResult consume(const ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] WindowsZeroCopyFrameConsumer callback();
  [[nodiscard]] WindowsNativeConsumerTelemetry telemetry() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class WindowsHardwareEncoderConsumer final {
public:
  using SubmitCallback = std::function<DigitorResult(
      const WindowsD3D12FrameLease&)>;

  WindowsHardwareEncoderConsumer(WindowsEncoderConsumerConfig,
                                 WindowsD3D12LeaseProvider,
                                 SubmitCallback);
  ~WindowsHardwareEncoderConsumer();
  [[nodiscard]] DigitorResult consume(const ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] WindowsZeroCopyFrameConsumer callback();
  [[nodiscard]] DigitorResult flush() noexcept;
  [[nodiscard]] WindowsNativeConsumerTelemetry telemetry() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct WindowsZeroCopyNativeBinding {
  WindowsZeroCopyDecodeCallback decode;
  WindowsD3D12LeaseProvider lease_provider;
  WindowsPreviewConsumerConfig preview;
  WindowsEncoderConsumerConfig encoder;
  WindowsHardwareEncoderConsumer::SubmitCallback encoder_submit;
};

class WindowsZeroCopyNativePipeline final {
public:
  WindowsZeroCopyNativePipeline(WindowsZeroCopyRuntimeConfig,
                                WindowsZeroCopyNativeBinding);
  ~WindowsZeroCopyNativePipeline();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult export_frame(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult flush_export() noexcept;
  [[nodiscard]] WindowsZeroCopyRuntimeTelemetry runtime_telemetry() const;
  [[nodiscard]] WindowsNativeConsumerTelemetry preview_telemetry() const;
  [[nodiscard]] WindowsNativeConsumerTelemetry encoder_telemetry() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
