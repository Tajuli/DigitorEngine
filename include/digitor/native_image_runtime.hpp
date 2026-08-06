#pragma once

#include "digitor/gpu_frame.hpp"
#include "digitor/image_editor_runtime.hpp"
#include "digitor/image_io.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class NativeImagePlatform : std::uint8_t { windows, android, apple, portable };
enum class NativeImageOrientation : std::uint8_t {
  normal = 1, mirror_horizontal = 2, rotate_180 = 3, mirror_vertical = 4,
  mirror_horizontal_rotate_270 = 5, rotate_90 = 6,
  mirror_horizontal_rotate_90 = 7, rotate_270 = 8
};

struct NativeImageMetadata {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint16_t bits_per_channel{8};
  std::uint16_t channel_count{4};
  NativeImageOrientation orientation{NativeImageOrientation::normal};
  bool has_alpha{};
  bool premultiplied_alpha{};
  std::string mime_type;
  std::string color_space{"sRGB"};
  std::vector<std::uint8_t> icc_profile;
};

struct NativeImageLimits {
  std::uint32_t max_dimension{65535};
  std::uint64_t max_pixels{400000000ULL};
  std::uint64_t max_decoded_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint32_t tile_width{512};
  std::uint32_t tile_height{512};
};

struct NativeImageProgress {
  enum class Stage : std::uint8_t { probe, decode, upload, process, readback, encode, complete };
  Stage stage{Stage::probe};
  std::uint64_t completed{};
  std::uint64_t total{};
};

struct NativeImageCancellation {
  std::shared_ptr<std::atomic_bool> flag{std::make_shared<std::atomic_bool>(false)};
  void cancel() const noexcept { if (flag) flag->store(true, std::memory_order_release); }
  [[nodiscard]] bool cancelled() const noexcept {
    return flag && flag->load(std::memory_order_acquire);
  }
};

struct NativeImageTile {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
};

struct NativeImageDecodedFrame {
  NativeImageMetadata metadata;
  RenderVideoFrame cpu_frame;
  ProcessedGpuFramePtr gpu_frame;
  [[nodiscard]] bool gpu_resident() const noexcept { return static_cast<bool>(gpu_frame); }
  [[nodiscard]] bool valid() const noexcept {
    return gpu_resident() || (cpu_frame.valid() && !cpu_frame.gpu_resident());
  }
};

struct NativeImageCodecServices {
  NativeImagePlatform platform{NativeImagePlatform::portable};
  std::string implementation_name;
  std::function<ImageIoResult(const std::string&, NativeImageMetadata&)> probe;
  std::function<ImageIoResult(const std::string&, const NativeImageMetadata&,
                              const NativeImageLimits&, const NativeImageCancellation&,
                              const std::function<void(const NativeImageProgress&)>&,
                              NativeImageDecodedFrame&)> decode;
  std::function<ImageIoResult(const NativeImageDecodedFrame&, const std::string&,
                              const ImageExportOptions&, const NativeImageMetadata&,
                              const NativeImageCancellation&,
                              const std::function<void(const NativeImageProgress&)>&)> encode;
};

struct NativeImageRuntimeConfig {
  NativeImageCodecServices codec;
  ImageEditorRuntimeConfig editor;
  NativeImageLimits limits;
  NativeImageCancellation cancellation;
  std::function<void(const NativeImageProgress&)> progress;
};

struct NativeImageRuntimeSnapshot {
  ImageEditorExecutionBackend backend{ImageEditorExecutionBackend::cpu};
  NativeImagePlatform platform{NativeImagePlatform::portable};
  NativeImageMetadata metadata;
  std::string codec;
  bool open{};
  bool cancelled{};
};

class NativeImageRuntime final {
 public:
  using OpenResult = std::pair<std::unique_ptr<NativeImageRuntime>, ImageIoResult>;
  static OpenResult open(std::string path, NativeImageRuntimeConfig config);

  NativeImageRuntime(const NativeImageRuntime&) = delete;
  NativeImageRuntime& operator=(const NativeImageRuntime&) = delete;

  [[nodiscard]] NativeImageRuntimeSnapshot snapshot() const;
  void set_graph_revision(std::uint64_t revision) noexcept;
  void set_parameter_revision(std::uint64_t revision) noexcept;
  [[nodiscard]] std::optional<ImageEditorPreviewFrame> render_preview(
      std::uint32_t width = 0, std::uint32_t height = 0,
      std::int64_t timestamp_us = 0, std::string* diagnostic = nullptr);
  ImageIoResult export_image(const std::string& output_path,
                             const ImageExportOptions& options = {},
                             std::string* diagnostic = nullptr);
  void cancel() noexcept { config_.cancellation.cancel(); }

  [[nodiscard]] static std::vector<NativeImageTile> tiles(
      std::uint32_t width, std::uint32_t height,
      std::uint32_t tile_width, std::uint32_t tile_height);

 private:
  NativeImageRuntime(std::string path, NativeImageRuntimeConfig config,
                     NativeImageMetadata metadata,
                     std::unique_ptr<ImageEditorRuntime> editor)
      : path_(std::move(path)), config_(std::move(config)),
        metadata_(std::move(metadata)), editor_(std::move(editor)) {}

  static ImageIoResult validate_metadata(const NativeImageMetadata&,
                                         const NativeImageLimits&);
  void report(NativeImageProgress::Stage, std::uint64_t, std::uint64_t) const;

  std::string path_;
  NativeImageRuntimeConfig config_;
  NativeImageMetadata metadata_;
  std::unique_ptr<ImageEditorRuntime> editor_;
};

NativeImageCodecServices default_native_image_codec_services();

} // namespace digitor
