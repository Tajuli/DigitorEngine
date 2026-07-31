#pragma once

#include "digitor/windows_d3d12_p010_converter.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct WindowsD3D12P010DispatchConfig {
  void* device{};                 // ID3D12Device*
  std::string shader_path;        // RGBA16F -> P010 compute shader
  std::string entry_point{"main"};
  std::uint32_t descriptor_count{3};
  bool require_typed_uav_loads{true};
};

struct WindowsD3D12P010DispatchTelemetry {
  std::uint64_t submissions{};
  std::uint64_t completed{};
  std::uint64_t shader_compile_failures{};
  std::uint64_t unsupported_format_failures{};
  std::uint64_t resource_state_failures{};
  std::uint64_t device_lost_events{};
  std::uint64_t cpu_copies{};
  std::string diagnostic;
};

class WindowsD3D12P010Dispatch final {
public:
  explicit WindowsD3D12P010Dispatch(WindowsD3D12P010DispatchConfig);
  ~WindowsD3D12P010Dispatch();

  WindowsD3D12P010Dispatch(const WindowsD3D12P010Dispatch&) = delete;
  WindowsD3D12P010Dispatch& operator=(const WindowsD3D12P010Dispatch&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult dispatch(
      void* rgba16f_resource,
      void* p010_resource,
      const WindowsP010GpuConstants&,
      void* command_queue,
      void* completion_fence,
      std::uint64_t completion_value) noexcept;
  [[nodiscard]] WindowsP010GpuDispatch callback();
  [[nodiscard]] WindowsD3D12P010DispatchTelemetry telemetry() const;
  [[nodiscard]] bool gpu_only() const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
