#pragma once

#include "digitor/image_io.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace digitor {

// Production still-image path. Platform hosts decode/import JPEG, PNG or WebP
// directly into a backend-native texture. Once a GPU backend is selected this
// runtime never falls back to CPU decoding, CPU scaling or CPU compositing.
struct GpuStillImageHost {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::string device_identity;

  std::function<std::optional<ProcessedGpuFramePtr>(
      const std::string& path,
      std::int64_t timestamp_us,
      std::string& diagnostic)> decode_and_upload;

  // Deterministic GPU resize/transform. Preview and export must invoke the same
  // kernel, precision, sampling convention and color metadata.
  std::function<DigitorResult(
      const ProcessedGpuFramePtr& source,
      std::uint32_t width,
      std::uint32_t height,
      std::int64_t timestamp_us,
      ProcessedGpuFramePtr& output,
      std::string& diagnostic)> resize;

  // Image-file export is an explicit terminal operation. The host may use a
  // hardware encoder or one intentional staging readback; normal preview/video
  // export must never call this callback.
  std::function<ImageIoResult(
      const ProcessedGpuFramePtr& processed,
      const std::string& output_path,
      const ImageExportOptions& options)> encode_image;
};

class GpuStillImageAsset final {
 public:
  static std::pair<std::shared_ptr<GpuStillImageAsset>, ImageIoResult> open(
      std::string path, GpuStillImageHost host);

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] DigitorRendererBackend backend() const noexcept { return host_.backend; }
  [[nodiscard]] ProcessedGpuFramePtr source_frame() const noexcept { return source_; }

  [[nodiscard]] std::optional<RenderVideoFrame> render_frame(
      std::uint32_t width,
      std::uint32_t height,
      std::int64_t timestamp_us,
      std::string* diagnostic = nullptr) const;

  ImageIoResult export_image(const ProcessedGpuFramePtr& processed,
                             const std::string& output_path,
                             const ImageExportOptions& options = {}) const;

 private:
  GpuStillImageAsset(std::string path, GpuStillImageHost host,
                     ProcessedGpuFramePtr source);

  std::string path_;
  GpuStillImageHost host_;
  ProcessedGpuFramePtr source_;
};

// Produces the callback used by TimelineMediaAdapter. It returns GPU-resident
// frames only. Any CPU frame or backend/context mismatch is rejected.
class GpuStillImageTimelineCache final {
 public:
  explicit GpuStillImageTimelineCache(GpuStillImageHost host);

  ImageIoResult register_clip(const std::string& clip_id,
                              const std::string& path);
  bool remove_clip(const std::string& clip_id) noexcept;
  void clear() noexcept;

  [[nodiscard]] std::function<std::optional<RenderVideoFrame>(
      const MediaDecodeRequest&)> decoder_callback();

 private:
  GpuStillImageHost host_;
  std::unordered_map<std::string, std::shared_ptr<GpuStillImageAsset>> assets_;
  mutable std::mutex mutex_;
};

}  // namespace digitor
