#pragma once

#include "digitor/windows_zero_copy_native_consumers.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

struct WindowsD3D12ProducedFrame {
  void* rgba16f_resource{};
  void* producer_fence{};
  std::uint64_t fence_value{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> lifetime;
};

class WindowsD3D12LeaseRegistry final {
public:
  WindowsD3D12LeaseRegistry();
  ~WindowsD3D12LeaseRegistry();
  WindowsD3D12LeaseRegistry(const WindowsD3D12LeaseRegistry&) = delete;
  WindowsD3D12LeaseRegistry& operator=(const WindowsD3D12LeaseRegistry&) = delete;
  [[nodiscard]] DigitorResult publish(WindowsD3D12ProducedFrame) noexcept;
  void retire(std::uint64_t frame_identity) noexcept;
  void clear() noexcept;
  [[nodiscard]] WindowsD3D12LeaseProvider provider();
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct WindowsD3D12SwapchainPresenterConfig {
  void* device{};
  void* command_queue{};
  void* swapchain{};
  std::uint32_t back_buffer_count{3};
  bool allow_tearing{};
};

using WindowsD3D12PresentCallback = std::function<DigitorResult(
    const WindowsD3D12FrameLease&)>;

class WindowsD3D12SwapchainPresenter final {
public:
  explicit WindowsD3D12SwapchainPresenter(WindowsD3D12SwapchainPresenterConfig);
  ~WindowsD3D12SwapchainPresenter();
  [[nodiscard]] DigitorResult present(const WindowsD3D12FrameLease&) noexcept;
  [[nodiscard]] WindowsD3D12PresentCallback callback();
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct WindowsP010EncoderSurface {
  void* resource{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> lifetime;
};

using WindowsRgba16fToP010 = std::function<DigitorResult(
    const WindowsD3D12FrameLease&, WindowsP010EncoderSurface&)>;

struct WindowsMediaFoundationEncoderConfig {
  void* dxgi_device_manager{};
  std::string output_path;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps_num{30};
  std::uint32_t fps_den{1};
  std::uint32_t bitrate{20000000};
  bool hevc{true};
  bool main10{true};
};

class WindowsMediaFoundationHardwareEncoder final {
public:
  WindowsMediaFoundationHardwareEncoder(WindowsMediaFoundationEncoderConfig,
                                        WindowsRgba16fToP010);
  ~WindowsMediaFoundationHardwareEncoder();
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult submit(const WindowsD3D12FrameLease&) noexcept;
  [[nodiscard]] WindowsHardwareEncoderConsumer::SubmitCallback callback();
  [[nodiscard]] DigitorResult flush() noexcept;
  [[nodiscard]] std::uint64_t submitted_frames() const noexcept;
private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
