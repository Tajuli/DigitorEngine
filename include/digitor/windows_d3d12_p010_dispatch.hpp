#pragma once

#include "digitor/windows_d3d12_p010_converter.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct WindowsD3D12P010DispatchConfig {
  void* device{};                 // ID3D12Device*
  DigitorPixelFormat input_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  // Optional source-file override retained for qualification tools. Runtime
  // production may leave this empty and use the embedded shader source.
  std::string shader_path;
  std::string entry_point{"main"};
  std::uint32_t descriptor_count{3};
  bool require_typed_uav_loads{true};
  // Final renderer output is already shader-readable; shared presentation
  // output is returned in COMMON. Keep this explicit so the dispatch never
  // guesses a resource state or silently performs a CPU staging copy.
  bool source_starts_shader_readable{};
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
      void* rgba_resource,
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
