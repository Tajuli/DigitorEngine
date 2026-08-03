#pragma once

#include "digitor/consumer_plugin_runtime.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace digitor {

enum class PluginPixelFormat : std::uint32_t {
  rgba8_unorm,
  bgra8_unorm,
  rgba16_float,
  rgba32_float
};

enum class PluginColorPrimaries : std::uint32_t {
  bt709,
  display_p3,
  bt2020
};

enum class PluginTransferFunction : std::uint32_t {
  linear,
  srgb,
  gamma24,
  pq,
  hlg
};

enum class PluginColorRange : std::uint32_t { full, limited };
enum class PluginAlphaMode : std::uint32_t { opaque, straight, premultiplied };

struct PluginGpuFrame final {
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  std::uint64_t native_texture_handle{};
  std::uint64_t synchronization_handle{};
  std::uint64_t synchronization_value{};
  std::uint32_t width{};
  std::uint32_t height{};
  PluginPixelFormat format{PluginPixelFormat::rgba8_unorm};
  PluginColorPrimaries primaries{PluginColorPrimaries::bt709};
  PluginTransferFunction transfer{PluginTransferFunction::srgb};
  PluginColorRange range{PluginColorRange::full};
  PluginAlphaMode alpha{PluginAlphaMode::straight};
  std::int64_t timestamp_us{};
};

struct PluginZeroCopyRequest final {
  ConsumerPluginInstance instance;
  ConsumerPluginSurface surface{ConsumerPluginSurface::preview};
  std::string project_or_clip_id;
  std::string visual_stack_digest;
  PluginGpuFrame input;
  PluginGpuFrame output;
};

struct PluginZeroCopyTelemetry final {
  std::uint64_t preview_frames{};
  std::uint64_t export_frames{};
  std::uint64_t gpu_dispatches{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t cpu_fallback_frames{};
  std::uint64_t parity_failures{};
  std::string diagnostic;
};

using PluginZeroCopyDispatch = std::function<DigitorResult(
    const PluginZeroCopyRequest&, std::string& diagnostic)>;

struct PluginZeroCopyBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  PluginZeroCopyDispatch dispatch;
};

class PluginZeroCopyFrameRuntime final {
 public:
  explicit PluginZeroCopyFrameRuntime(PluginZeroCopyBindings bindings);

  [[nodiscard]] DigitorResult process(
      const PluginZeroCopyRequest& request,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] PluginZeroCopyTelemetry telemetry() const;

 private:
  struct ParityRecord final {
    std::string stack_digest;
    std::string plugin_id;
    std::string plugin_version;
    PluginPixelFormat format{PluginPixelFormat::rgba8_unorm};
    PluginColorPrimaries primaries{PluginColorPrimaries::bt709};
    PluginTransferFunction transfer{PluginTransferFunction::srgb};
    PluginColorRange range{PluginColorRange::full};
    PluginAlphaMode alpha{PluginAlphaMode::straight};
  };

  bool validate_request(const PluginZeroCopyRequest&,
                        std::string& diagnostic) const noexcept;
  bool validate_parity(const PluginZeroCopyRequest&,
                       std::string& diagnostic) noexcept;

  PluginZeroCopyBindings bindings_;
  PluginZeroCopyTelemetry telemetry_;
  std::unordered_map<std::int64_t, ParityRecord> preview_records_;
  std::unordered_map<std::int64_t, ParityRecord> export_records_;
};

}  // namespace digitor
