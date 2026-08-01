#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

enum class AppleOutputTransfer : std::uint32_t { sdr=1, pq=2, hlg=3 };
enum class AppleOutputMatrix : std::uint32_t { bt709=1, bt2020_ncl=2 };
enum class AppleOutputRange : std::uint32_t { video=1, full=2 };

struct AppleMetalP010Config {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t pool_capacity{6};
  AppleOutputTransfer transfer{AppleOutputTransfer::sdr};
  AppleOutputMatrix matrix{AppleOutputMatrix::bt709};
  AppleOutputRange range{AppleOutputRange::video};
  float mastering_peak_nits{1000.0f};
  std::string engine_commit;
};

struct AppleMetalP010NativeContext {
  void* metal_device{};        // id<MTLDevice>
  void* command_queue{};       // id<MTLCommandQueue>
  void* compression_session{}; // VTCompressionSessionRef
};

struct AppleMetalP010Frame {
  void* pixel_buffer{};   // CVPixelBufferRef, IOSurface-backed P010
  void* luma_texture{};   // id<MTLTexture>, R16Unorm
  void* chroma_texture{}; // id<MTLTexture>, RG16Unorm
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp_us{};
  std::uint64_t frame_identity{};
  std::shared_ptr<void> lifetime;
};

struct AppleMetalP010Telemetry {
  std::uint64_t pool_acquires{};
  std::uint64_t shader_dispatches{};
  std::uint64_t encoder_submissions{};
  std::uint64_t failures{};
  std::uint64_t cpu_copies{};
  std::uint64_t cpu_fallback_frames{};
  bool quarantined{};
  std::string diagnostic;
};

class AppleMetalP010Pipeline final {
public:
  AppleMetalP010Pipeline(AppleMetalP010Config, AppleMetalP010NativeContext);
  ~AppleMetalP010Pipeline();
  AppleMetalP010Pipeline(const AppleMetalP010Pipeline&) = delete;
  AppleMetalP010Pipeline& operator=(const AppleMetalP010Pipeline&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult convert_and_encode(const ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] DigitorResult reset_quarantine() noexcept;
  [[nodiscard]] AppleMetalP010Telemetry telemetry() const;
  [[nodiscard]] bool production_active() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
