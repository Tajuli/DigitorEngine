#pragma once

#include "digitor/windows_zero_copy_concrete_bindings.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

enum class WindowsOutputMatrix : std::uint32_t {
  bt709 = 1,
  bt2020_ncl = 2,
};

enum class WindowsOutputTransfer : std::uint32_t {
  gamma24 = 1,
  pq = 2,
  hlg = 3,
};

struct WindowsP010ConversionConfig {
  void* d3d12_device{};       // ID3D12Device*
  void* command_queue{};      // ID3D12CommandQueue*
  void* d3d11_device{};       // ID3D11Device* used by Media Foundation
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t pool_size{6};
  WindowsOutputMatrix matrix{WindowsOutputMatrix::bt709};
  WindowsOutputTransfer transfer{WindowsOutputTransfer::gamma24};
  bool full_range{};
  bool preserve_superwhites{true};
  float mastering_peak_nits{100.0f};
};

struct WindowsP010ConverterTelemetry {
  std::uint64_t submitted{};
  std::uint64_t completed{};
  std::uint64_t pool_exhaustions{};
  std::uint64_t synchronization_failures{};
  std::uint64_t device_lost_events{};
  std::uint64_t cpu_copies{};
  std::string diagnostic;
};

class WindowsD3D12P010Converter final {
public:
  explicit WindowsD3D12P010Converter(WindowsP010ConversionConfig);
  ~WindowsD3D12P010Converter();

  WindowsD3D12P010Converter(const WindowsD3D12P010Converter&) = delete;
  WindowsD3D12P010Converter& operator=(const WindowsD3D12P010Converter&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult convert(const WindowsD3D12FrameLease&,
                                      WindowsP010EncoderSurface&) noexcept;
  [[nodiscard]] WindowsRgba16fToP010 callback();
  [[nodiscard]] WindowsP010ConverterTelemetry telemetry() const;
  [[nodiscard]] bool gpu_only() const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
