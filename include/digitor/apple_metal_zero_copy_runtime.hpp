#pragma once

#include "digitor/apple_native_zero_copy.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

struct AppleMetalRuntimeConfig {
  void* metal_device{};
  void* command_queue{};
  void* compression_session{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t frame_rate_num{30};
  std::uint32_t frame_rate_den{1};
  std::uint32_t max_in_flight{6};
  bool require_p010{true};
  bool require_hdr_metadata{false};
  // These callbacks are owned by the platform/renderer that owns the actual
  // MTLDevice, Flutter texture and VTCompressionSession resources. The runtime
  // validates their GPU-resident outputs and never fabricates successful work.
  AppleYuvToRgba16f yuv_to_rgba16f;
  AppleGpuConsumer preview_present;
  AppleGpuConsumer encoder_submit;
};

struct AppleMetalRuntimeTelemetry {
  std::uint64_t nv12_dispatches{};
  std::uint64_t p010_dispatches{};
  std::uint64_t rgba16f_frames{};
  std::uint64_t p010_encoder_frames{};
  std::uint64_t preview_frames{};
  std::uint64_t command_failures{};
  std::uint64_t pool_exhaustions{};
  std::uint64_t cpu_copies{};
  std::uint64_t cpu_fallbacks{};
  std::string diagnostic;
};

class AppleMetalZeroCopyRuntime final {
 public:
  explicit AppleMetalZeroCopyRuntime(AppleMetalRuntimeConfig);
  ~AppleMetalZeroCopyRuntime();
  AppleMetalZeroCopyRuntime(const AppleMetalZeroCopyRuntime&) = delete;
  AppleMetalZeroCopyRuntime& operator=(const AppleMetalZeroCopyRuntime&) = delete;
  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] AppleYuvToRgba16f rgba16f_dispatch() const;
  [[nodiscard]] AppleGpuConsumer preview_consumer() const;
  [[nodiscard]] AppleGpuConsumer encoder_consumer() const;
  [[nodiscard]] AppleMetalRuntimeTelemetry telemetry() const;
  [[nodiscard]] bool gpu_only() const noexcept;
 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace digitor
