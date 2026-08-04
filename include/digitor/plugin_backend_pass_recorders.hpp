#pragma once

#include "digitor/plugin_native_pass_dispatch.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace digitor {

struct PluginBackendCommandContext final {
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  std::uint64_t device_identity{};
  std::uint64_t command_context_handle{};
  bool provider_owned{};
  bool external_synchronization{};
};

using PluginBackendRecordD3D12 = std::function<DigitorResult(
    const PluginNativeDispatch&, const PluginBackendCommandContext&,
    std::string& diagnostic)>;
using PluginBackendRecordVulkan = PluginBackendRecordD3D12;
using PluginBackendRecordMetal = PluginBackendRecordD3D12;
using PluginBackendRecordGles = PluginBackendRecordD3D12;

struct PluginBackendPassRecorderBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  std::uint64_t device_identity{};
  PluginBackendCommandContext command_context;
  PluginBackendRecordD3D12 record_d3d12;
  PluginBackendRecordVulkan record_vulkan;
  PluginBackendRecordMetal record_metal;
  PluginBackendRecordGles record_gles;
};

struct PluginBackendPassRecorderTelemetry final {
  std::uint64_t recorded_passes{};
  std::uint64_t failed_passes{};
  std::uint64_t descriptor_or_resource_bindings{};
  std::uint64_t synchronization_bindings{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t fallback_dispatches{};
  std::string diagnostic;
};

class PluginBackendPassRecorder final {
 public:
  explicit PluginBackendPassRecorder(PluginBackendPassRecorderBindings bindings);

  [[nodiscard]] DigitorResult record(
      const PluginNativeDispatch& dispatch,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] PluginBackendPassRecorderTelemetry telemetry() const;

 private:
  PluginBackendPassRecorderBindings bindings_;
  PluginBackendPassRecorderTelemetry telemetry_;
};

[[nodiscard]] PluginNativeRecordDispatch make_plugin_backend_record_callback(
    PluginBackendPassRecorder& recorder);

}  // namespace digitor
