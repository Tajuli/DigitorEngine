#pragma once

#include "digitor/plugin_backend_package_loader.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace digitor {

struct PluginNativeTextureBinding final {
  std::uint64_t native_texture_handle{};
  std::uint64_t synchronization_handle{};
  std::uint64_t synchronization_value{};
  std::uint32_t width{};
  std::uint32_t height{};
  PluginPixelFormat format{PluginPixelFormat::rgba8_unorm};
};

struct PluginNativeDispatch final {
  PluginBackendPipeline pipeline;
  PluginGpuPassDescriptor pass;
  std::uint32_t pass_index{};
  PluginNativeTextureBinding input;
  PluginNativeTextureBinding output;
  std::uint32_t group_count_x{};
  std::uint32_t group_count_y{};
  std::uint32_t group_count_z{1};
  std::unordered_map<std::string, double> parameters;
};

using PluginNativeRecordDispatch = std::function<DigitorResult(
    const PluginNativeDispatch&, std::string& diagnostic)>;

struct PluginNativePassDispatchBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  std::uint64_t device_identity{};
  PluginNativeRecordDispatch record_dispatch;
};

struct PluginNativePassDispatchTelemetry final {
  std::uint64_t recorded_passes{};
  std::uint64_t failed_passes{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t fallback_dispatches{};
  std::string diagnostic;
};

class PluginNativePassDispatcher final {
 public:
  PluginNativePassDispatcher(const PluginBackendPackageLoader& loader,
                             PluginNativePassDispatchBindings bindings);

  [[nodiscard]] DigitorResult record(
      const PluginGpuDispatchPass& pass,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] PluginNativePassDispatchTelemetry telemetry() const;

 private:
  const PluginBackendPackageLoader& loader_;
  PluginNativePassDispatchBindings bindings_;
  PluginNativePassDispatchTelemetry telemetry_;
};

[[nodiscard]] PluginGpuRecordPass make_plugin_native_record_pass(
    PluginNativePassDispatcher& dispatcher);

}  // namespace digitor
