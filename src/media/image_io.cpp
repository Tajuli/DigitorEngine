#include "digitor/image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef DIGITOR_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace digitor {
namespace {

float clamp01(float value) noexcept {
  return std::clamp(value, 0.0F, 1.0F);
}

std::string lowercase_extension(const std::string& path) {
  auto extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return extension;
}

RenderVideoFrame scale_frame(const VideoFrame& source, std::uint32_t target_width,
                             std::uint32_t target_height, const std::string& provenance) {
  const std::uint32_t source_width = source.width;
  const std::uint32_t source_height = source.height;
  if (target_width == 0) target_width = source_width;
  if (target_height == 0) target_height = source_height;
  if (source_width == 0 || source_height == 0 || source.pixels.empty()) return {};

  RenderVideoFrame output;
  output.width = target_width;
  output.height = target_height;
  output.provenance = provenance;
  output.rgba.resize(static_cast<std::size_t>(target_width) * target_height * 4U);

  const float x_scale = static_cast<float>(source_width) / static_cast<float>(target_width);
  const float y_scale = static_cast<float>(source_height) / static_cast<float>(target_height);
  for (std::uint32_t y = 0; y < target_height; ++y) {
    const float source_y = (static_cast<float>(y) + 0.5F) * y_scale - 0.5F;
    const auto y0 = static_cast<std::uint32_t>(std::clamp(std::floor(source_y), 0.0F,
                                                         static_cast<float>(source_height - 1U)));
    const auto y1 = std::min(y0 + 1U, source_height - 1U);
    const float fy = std::clamp(source_y - static_cast<float>(y0), 0.0F, 1.0F);
    for (std::uint32_t x = 0; x < target_width; ++x) {
      const float source_x = (static_cast<float>(x) + 0.5F) * x_scale - 0.5F;
      const auto x0 = static_cast<std::uint32_t>(std::clamp(std::floor(source_x), 0.0F,
                                                           static_cast<float>(source_width - 1U)));
      const auto x1 = std::min(x0 + 1U, source_width - 1U);
      const float fx = std::clamp(source_x - static_cast<float>(x0), 0.0F, 1.0F);
      const auto& c00 = source.pixels[static_cast<std::size_t>(y0) * source_width + x0];
      const auto& c10 = source.pixels[static_cast<std::size_t>(y0) * source_width + x1];
      const auto& c01 = source.pixels[static_cast<std::size_t>(y1) * source_width + x0];
      const auto& c11 = source.pixels[static_cast<std::size_t>(y1) * source_width + x1];
      const auto interpolate = [fx, fy](float p00, float p10, float p01, float p11) {
        const float top = p00 + (p10 - p00) * fx;
        const float bottom = p01 + (p11 - p01) * fx;
        return top + (bottom - top) * fy;
      };
      const std::size_t index = (static_cast<std::size_t>(y) * target_width + x) * 4U;
      output.rgba[index + 0U] = interpolate(c00.r, c10.r, c01.r, c11.r);
      output.rgba[index + 1U] = interpolate(c00.g, c10.g, c01.g, c11.g);
      output.rgba[index + 2U] = interpolate(c00.b, c10.b, c01.b, c11.b);
      output.rgba[index + 3U] = interpolate(c00.a, c10.a, c01.a, c11.a);
    }
  }
  return output;
}

#ifdef DIGITOR_HAS_FFMPEG
std::string ffmpeg_error(int value) {
  char text[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(value, text, sizeof(text));
  return text;
}

AVCodecID codec_for(ImageExportFormat format) noexcept {
  switch (format) {
    case ImageExportFormat::jpeg: return AV_CODEC_ID_MJPEG;
    case ImageExportFormat::png: return AV_CODEC_ID_PNG;
    case ImageExportFormat::webp: return AV_CODEC_ID_WEBP;
  }
  return AV_CODEC_ID_NONE;
}

AVPixelFormat preferred_pixel_format(ImageExportFormat format, bool preserve_alpha) noexcept {
  if (format == ImageExportFormat::png || (format == ImageExportFormat::webp && preserve_alpha)) {
    return AV_PIX_FMT_RGBA;
  }
  return AV_PIX_FMT_YUVJ420P;
}

bool codec_supports(const AVCodec* codec, AVPixelFormat format) noexcept {
  if (!codec || !codec->pix_fmts) return true;
  for (const AVPixelFormat* current = codec->pix_fmts; *current != AV_PIX_FMT_NONE; ++current) {
    if (*current == format) return true;
  }
  return false;
}

ImageIoResult encode_ffmpeg(const RenderVideoFrame& frame, const std::string& output_path,
                            const ImageExportOptions& options) {
  if (!frame.valid() || frame.gpu_resident()) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "image export requires a valid CPU linear-RGBA frame; perform explicit GPU readback first"};
  }
  if (output_path.empty()) return {DIGITOR_RESULT_INVALID_ARGUMENT, "output path is empty"};
  if (!options.overwrite && std::filesystem::exists(output_path)) {
    return {DIGITOR_RESULT_RESOURCE_IN_USE, "output file already exists"};
  }
  if (options.quality < 1 || options.quality > 100) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "quality must be between 1 and 100"};
  }

  AVFormatContext* format = nullptr;
  AVCodecContext* codec_context = nullptr;
  AVFrame* source_frame = nullptr;
  AVFrame* encoded_frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scaler = nullptr;
  auto cleanup = [&]() {
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&encoded_frame);
    av_frame_free(&source_frame);
    avcodec_free_context(&codec_context);
    if (format) {
      if (format->pb) avio_closep(&format->pb);
      avformat_free_context(format);
    }
  };

  const AVCodecID codec_id = codec_for(options.format);
  const AVCodec* codec = avcodec_find_encoder(codec_id);
  if (!codec) return {DIGITOR_RESULT_UNSUPPORTED, "requested FFmpeg image encoder is unavailable"};

  int result = avformat_alloc_output_context2(&format, nullptr, "image2", output_path.c_str());
  if (result < 0 || !format) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot create image output: " + ffmpeg_error(result)};
  }
  AVStream* stream = avformat_new_stream(format, nullptr);
  if (!stream) {
    cleanup();
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "cannot allocate image output stream"};
  }
  codec_context = avcodec_alloc_context3(codec);
  if (!codec_context) {
    cleanup();
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "cannot allocate image encoder"};
  }

  const std::uint32_t width = options.width == 0 ? frame.width : options.width;
  const std::uint32_t height = options.height == 0 ? frame.height : options.height;
  AVPixelFormat pixel_format = preferred_pixel_format(options.format, options.preserve_alpha);
  if (!codec_supports(codec, pixel_format)) {
    pixel_format = codec->pix_fmts ? codec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
  }
  codec_context->codec_id = codec_id;
  codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_context->width = static_cast<int>(width);
  codec_context->height = static_cast<int>(height);
  codec_context->pix_fmt = pixel_format;
  codec_context->time_base = AVRational{1, 1};
  codec_context->framerate = AVRational{1, 1};
  codec_context->color_range = AVCOL_RANGE_JPEG;
  if (options.format == ImageExportFormat::jpeg || options.format == ImageExportFormat::webp) {
    codec_context->global_quality = FF_QP2LAMBDA * std::max(1, 31 - options.quality * 30 / 100);
    codec_context->flags |= AV_CODEC_FLAG_QSCALE;
  }
  if (format->oformat->flags & AVFMT_GLOBALHEADER) codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  result = avcodec_open2(codec_context, codec, nullptr);
  if (result < 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot open image encoder: " + ffmpeg_error(result)};
  }
  result = avcodec_parameters_from_context(stream->codecpar, codec_context);
  if (result < 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot configure image stream: " + ffmpeg_error(result)};
  }
  stream->time_base = codec_context->time_base;

  if (!(format->oformat->flags & AVFMT_NOFILE)) {
    result = avio_open(&format->pb, output_path.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
      cleanup();
      return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot open output file: " + ffmpeg_error(result)};
    }
  }
  result = avformat_write_header(format, nullptr);
  if (result < 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot write image header: " + ffmpeg_error(result)};
  }

  source_frame = av_frame_alloc();
  encoded_frame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!source_frame || !encoded_frame || !packet) {
    cleanup();
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "cannot allocate image conversion resources"};
  }
  source_frame->format = AV_PIX_FMT_RGBA;
  source_frame->width = static_cast<int>(frame.width);
  source_frame->height = static_cast<int>(frame.height);
  encoded_frame->format = pixel_format;
  encoded_frame->width = static_cast<int>(width);
  encoded_frame->height = static_cast<int>(height);
  encoded_frame->pts = 0;
  if (av_frame_get_buffer(source_frame, 32) < 0 || av_frame_get_buffer(encoded_frame, 32) < 0) {
    cleanup();
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "cannot allocate image frame buffers"};
  }

  for (std::uint32_t y = 0; y < frame.height; ++y) {
    auto* row = source_frame->data[0] + static_cast<std::ptrdiff_t>(y) * source_frame->linesize[0];
    for (std::uint32_t x = 0; x < frame.width; ++x) {
      const std::size_t source_index = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
      const std::size_t destination_index = static_cast<std::size_t>(x) * 4U;
      row[destination_index + 0U] = static_cast<std::uint8_t>(std::lround(clamp01(frame.rgba[source_index + 0U]) * 255.0F));
      row[destination_index + 1U] = static_cast<std::uint8_t>(std::lround(clamp01(frame.rgba[source_index + 1U]) * 255.0F));
      row[destination_index + 2U] = static_cast<std::uint8_t>(std::lround(clamp01(frame.rgba[source_index + 2U]) * 255.0F));
      row[destination_index + 3U] = options.preserve_alpha
          ? static_cast<std::uint8_t>(std::lround(clamp01(frame.rgba[source_index + 3U]) * 255.0F))
          : 255U;
    }
  }

  scaler = sws_getContext(source_frame->width, source_frame->height, AV_PIX_FMT_RGBA,
                          encoded_frame->width, encoded_frame->height, pixel_format,
                          SWS_LANCZOS, nullptr, nullptr, nullptr);
  if (!scaler || sws_scale(scaler, source_frame->data, source_frame->linesize, 0,
                           source_frame->height, encoded_frame->data,
                           encoded_frame->linesize) <= 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot convert image pixels for encoder"};
  }

  result = avcodec_send_frame(codec_context, encoded_frame);
  if (result < 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot submit image frame: " + ffmpeg_error(result)};
  }
  result = avcodec_receive_packet(codec_context, packet);
  if (result < 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot receive encoded image: " + ffmpeg_error(result)};
  }
  packet->stream_index = stream->index;
  av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
  result = av_interleaved_write_frame(format, packet);
  av_packet_unref(packet);
  if (result >= 0) result = av_write_trailer(format);
  if (result < 0) {
    cleanup();
    return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot finalize image output: " + ffmpeg_error(result)};
  }
  cleanup();
  return {};
}
#endif

}  // namespace

StillImageAsset::StillImageAsset(std::string path, std::shared_ptr<VideoFrame> frame)
    : path_(std::move(path)), frame_(std::move(frame)) {}

std::pair<std::shared_ptr<StillImageAsset>, ImageIoResult> StillImageAsset::open(
    const std::string& path) {
  if (path.empty()) return {nullptr, {DIGITOR_RESULT_INVALID_ARGUMENT, "image path is empty"}};
  if (!supported_still_image_extension(path)) {
    return {nullptr, {DIGITOR_RESULT_UNSUPPORTED, "supported image extensions are JPEG, PNG and WebP"}};
  }
  if (!ffmpeg_available()) {
    return {nullptr, {DIGITOR_RESULT_UNSUPPORTED, "FFmpeg image provider is unavailable"}};
  }
  try {
    DecoderOptions options;
    options.hardware = HardwareDecode::cpu;
    options.allow_cpu_fallback = true;
    options.cache_capacity = 1;
    options.output_mode = DecodeOutputMode::cpu_rgba32f;
    options.require_zero_copy = false;
    auto decoder = open_video_decoder(path, options);
    auto frame = decoder ? decoder->decode(0) : nullptr;
    if (!frame || frame->width == 0 || frame->height == 0 || frame->pixels.empty()) {
      return {nullptr, {DIGITOR_RESULT_INTERNAL_ERROR, "image decoder returned no CPU RGBA pixels"}};
    }
    frame->number = 0;
    frame->pts = 0;
    frame->duration = std::numeric_limits<std::int64_t>::max();
    return {std::shared_ptr<StillImageAsset>(new StillImageAsset(path, std::move(frame))), {}};
  } catch (const std::exception& error) {
    return {nullptr, {DIGITOR_RESULT_INTERNAL_ERROR, error.what()}};
  }
}

std::uint32_t StillImageAsset::width() const noexcept { return frame_ ? frame_->width : 0; }
std::uint32_t StillImageAsset::height() const noexcept { return frame_ ? frame_->height : 0; }
std::shared_ptr<const VideoFrame> StillImageAsset::frame() const noexcept { return frame_; }

std::optional<RenderVideoFrame> StillImageAsset::render_frame(std::uint32_t width,
                                                               std::uint32_t height) const {
  if (!frame_) return std::nullopt;
  if (width == 0) width = frame_->width;
  if (height == 0) height = frame_->height;
  std::lock_guard lock(cache_mutex_);
  if (scaled_cache_ && scaled_cache_->width == width && scaled_cache_->height == height) {
    return scaled_cache_;
  }
  auto rendered = scale_frame(*frame_, width, height, "still-image:" + path_);
  if (!rendered.valid()) return std::nullopt;
  scaled_cache_ = rendered;
  return rendered;
}

ImageIoResult StillImageAsset::export_image(const std::string& output_path,
                                             const ImageExportOptions& options) const {
  auto rendered = render_frame(options.width, options.height);
  if (!rendered) return {DIGITOR_RESULT_INTERNAL_ERROR, "cannot render retained image"};
  ImageExportOptions direct = options;
  direct.width = 0;
  direct.height = 0;
  return export_image_frame(*rendered, output_path, direct);
}

ImageIoResult export_image_frame(const RenderVideoFrame& frame, const std::string& output_path,
                                 const ImageExportOptions& options) {
#ifdef DIGITOR_HAS_FFMPEG
  return encode_ffmpeg(frame, output_path, options);
#else
  (void)frame;
  (void)output_path;
  (void)options;
  return {DIGITOR_RESULT_UNSUPPORTED, "DigitorEngine was built without FFmpeg image encoders"};
#endif
}

bool image_io_available() noexcept { return ffmpeg_available(); }

bool supported_still_image_extension(const std::string& path) noexcept {
  try {
    const auto extension = lowercase_extension(path);
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
           extension == ".webp";
  } catch (...) {
    return false;
  }
}

}  // namespace digitor
