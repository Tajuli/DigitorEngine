#include "digitor/native_image_runtime.hpp"

#include <algorithm>
#include <limits>

namespace digitor {
namespace {
NativeImagePlatform host_platform() noexcept {
#if defined(_WIN32)
  return NativeImagePlatform::windows;
#elif defined(__ANDROID__)
  return NativeImagePlatform::android;
#elif defined(__APPLE__)
  return NativeImagePlatform::apple;
#else
  return NativeImagePlatform::portable;
#endif
}

const char* host_codec_name() noexcept {
#if defined(_WIN32)
  return "Windows image codec bridge";
#elif defined(__ANDROID__)
  return "Android image codec bridge";
#elif defined(__APPLE__)
  return "Apple ImageIO bridge";
#else
  return "portable FFmpeg image codec";
#endif
}
} // namespace

ImageIoResult NativeImageRuntime::validate_metadata(
    const NativeImageMetadata& m, const NativeImageLimits& limits) {
  if (!m.width || !m.height) return {DIGITOR_RESULT_INVALID_ARGUMENT, "image dimensions are zero"};
  if (!limits.tile_width || !limits.tile_height)
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "image tile dimensions are zero"};
  if (m.width > limits.max_dimension || m.height > limits.max_dimension)
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "image exceeds configured maximum dimension"};
  const auto pixels = static_cast<std::uint64_t>(m.width) * m.height;
  if (pixels > limits.max_pixels)
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "image exceeds configured pixel limit"};
  const auto channels = std::max<std::uint16_t>(m.channel_count, 1);
  const auto bytes_per_channel = std::max<std::uint16_t>(m.bits_per_channel, 8) / 8;
  if (pixels > std::numeric_limits<std::uint64_t>::max() / channels / bytes_per_channel ||
      pixels * channels * bytes_per_channel > limits.max_decoded_bytes)
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "decoded image exceeds configured memory limit"};
  const auto orientation = static_cast<unsigned>(m.orientation);
  if (orientation < 1 || orientation > 8)
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "invalid EXIF orientation"};
  return {};
}

void NativeImageRuntime::report(NativeImageProgress::Stage stage,
                                std::uint64_t completed,
                                std::uint64_t total) const {
  if (config_.progress) config_.progress({stage, completed, total});
}

NativeImageRuntime::OpenResult NativeImageRuntime::open(
    std::string path, NativeImageRuntimeConfig config) {
  if (path.empty())
    return {nullptr, {DIGITOR_RESULT_INVALID_ARGUMENT, "image path is empty"}};
  if (!config.codec.probe) config.codec = default_native_image_codec_services();
  if (!config.codec.probe)
    return {nullptr, {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "native image codec is unavailable"}};
  if (config.cancellation.cancelled())
    return {nullptr, {DIGITOR_RESULT_INTERNAL_ERROR, "image operation cancelled"}};

  NativeImageMetadata metadata;
  if (config.progress) config.progress({NativeImageProgress::Stage::probe, 0, 1});
  auto probe = config.codec.probe(path, metadata);
  if (!probe) return {nullptr, std::move(probe)};
  auto valid = validate_metadata(metadata, config.limits);
  if (!valid) return {nullptr, std::move(valid)};
  if (config.progress) config.progress({NativeImageProgress::Stage::probe, 1, 1});

  // ImageEditorRuntime performs the one-time GPU/CPU selection and locks it for
  // the session. A valid GPU host failing open/decode/upload is returned as an
  // error and never retried on CPU.
  auto [editor, result] = ImageEditorRuntime::open(path, std::move(config.editor));
  if (!result || !editor) return {nullptr, std::move(result)};

  return {std::unique_ptr<NativeImageRuntime>(new NativeImageRuntime(
              std::move(path), std::move(config), std::move(metadata),
              std::move(editor))), {}};
}

NativeImageRuntimeSnapshot NativeImageRuntime::snapshot() const {
  NativeImageRuntimeSnapshot value;
  value.platform = config_.codec.platform;
  value.metadata = metadata_;
  value.codec = config_.codec.implementation_name;
  value.cancelled = config_.cancellation.cancelled();
  if (editor_) {
    const auto state = editor_->snapshot();
    value.backend = state.backend;
    value.open = state.open;
  }
  return value;
}

void NativeImageRuntime::set_graph_revision(std::uint64_t revision) noexcept {
  if (editor_) editor_->set_graph_revision(revision);
}
void NativeImageRuntime::set_parameter_revision(std::uint64_t revision) noexcept {
  if (editor_) editor_->set_parameter_revision(revision);
}

std::optional<ImageEditorPreviewFrame> NativeImageRuntime::render_preview(
    std::uint32_t width, std::uint32_t height, std::int64_t timestamp_us,
    std::string* diagnostic) {
  if (!editor_ || config_.cancellation.cancelled()) {
    if (diagnostic) *diagnostic = "image preview cancelled or runtime closed";
    return std::nullopt;
  }
  report(NativeImageProgress::Stage::process, 0, 1);
  auto frame = editor_->render_preview(width, height, timestamp_us, diagnostic);
  if (frame) report(NativeImageProgress::Stage::process, 1, 1);
  return frame;
}

ImageIoResult NativeImageRuntime::export_image(
    const std::string& output_path, const ImageExportOptions& options,
    std::string* diagnostic) {
  if (!editor_) return {DIGITOR_RESULT_NOT_INITIALIZED, "image runtime is closed"};
  if (config_.cancellation.cancelled())
    return {DIGITOR_RESULT_INTERNAL_ERROR, "image export cancelled"};
  if (options.format == ImageExportFormat::jpeg && options.preserve_alpha && metadata_.has_alpha)
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "JPEG cannot preserve alpha; select flatten/non-alpha export"};
  report(NativeImageProgress::Stage::process, 0, 1);
  auto result = editor_->export_image(output_path, options, diagnostic);
  if (!result) return result;
  report(NativeImageProgress::Stage::encode, 1, 1);
  report(NativeImageProgress::Stage::complete, 1, 1);
  return result;
}

std::vector<NativeImageTile> NativeImageRuntime::tiles(
    std::uint32_t width, std::uint32_t height,
    std::uint32_t tile_width, std::uint32_t tile_height) {
  std::vector<NativeImageTile> result;
  if (!width || !height || !tile_width || !tile_height) return result;
  const auto columns = (static_cast<std::uint64_t>(width) + tile_width - 1) / tile_width;
  const auto rows = (static_cast<std::uint64_t>(height) + tile_height - 1) / tile_height;
  if (columns > std::numeric_limits<std::size_t>::max() / rows) return result;
  result.reserve(static_cast<std::size_t>(columns * rows));
  for (std::uint32_t y = 0; y < height; y += std::min(tile_height, height - y)) {
    for (std::uint32_t x = 0; x < width; x += std::min(tile_width, width - x)) {
      result.push_back({x, y, std::min(tile_width, width - x),
                        std::min(tile_height, height - y)});
    }
  }
  return result;
}

NativeImageCodecServices default_native_image_codec_services() {
  NativeImageCodecServices services;
  services.platform = host_platform();
  services.implementation_name = host_codec_name();
  services.probe = [](const std::string& path, NativeImageMetadata& metadata) {
    auto [asset, result] = StillImageAsset::open(path);
    if (!result || !asset) return result;
    metadata.width = asset->width();
    metadata.height = asset->height();
    metadata.bits_per_channel = 8;
    metadata.channel_count = 4;
    metadata.has_alpha = true;
    metadata.mime_type = supported_still_image_extension(path) ? "image/*" : "application/octet-stream";
    return ImageIoResult{};
  };
  services.decode = [](const std::string& path, const NativeImageMetadata& metadata,
                       const NativeImageLimits&, const NativeImageCancellation& cancellation,
                       const std::function<void(const NativeImageProgress&)>& progress,
                       NativeImageDecodedFrame& output) {
    if (cancellation.cancelled())
      return ImageIoResult{DIGITOR_RESULT_INTERNAL_ERROR, "image decode cancelled"};
    if (progress) progress({NativeImageProgress::Stage::decode, 0, 1});
    auto [asset, result] = StillImageAsset::open(path);
    if (!result || !asset) return result;
    auto frame = asset->render_frame();
    if (!frame) return ImageIoResult{DIGITOR_RESULT_INTERNAL_ERROR, "image decode failed"};
    output.metadata = metadata;
    output.cpu_frame = std::move(*frame);
    if (progress) progress({NativeImageProgress::Stage::decode, 1, 1});
    return ImageIoResult{};
  };
  services.encode = [](const NativeImageDecodedFrame& frame, const std::string& path,
                       const ImageExportOptions& options, const NativeImageMetadata&,
                       const NativeImageCancellation& cancellation,
                       const std::function<void(const NativeImageProgress&)>& progress) {
    if (cancellation.cancelled())
      return ImageIoResult{DIGITOR_RESULT_INTERNAL_ERROR, "image encode cancelled"};
    if (frame.gpu_resident())
      return ImageIoResult{DIGITOR_RESULT_INVALID_ARGUMENT,
                           "GPU frame must use the locked GPU session export path"};
    if (progress) progress({NativeImageProgress::Stage::encode, 0, 1});
    auto result = export_image_frame(frame.cpu_frame, path, options);
    if (result && progress) progress({NativeImageProgress::Stage::encode, 1, 1});
    return result;
  };
  return services;
}

} // namespace digitor
