#include "digitor/native_image_codec.hpp"

#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace digitor {
namespace {

bool cancelled(const NativeStillImageProgress& progress) noexcept {
  return progress.cancelled && progress.cancelled->load(std::memory_order_relaxed);
}

void report(const NativeStillImageProgress& progress, float value) {
  if (progress.report) progress.report(std::clamp(value, 0.0F, 1.0F));
}

std::uint32_t bytes_per_pixel(NativeImagePixelFormat format) noexcept {
  switch (format) {
    case NativeImagePixelFormat::rgba8:
    case NativeImagePixelFormat::bgra8: return 4;
    case NativeImagePixelFormat::rgba16: return 8;
    case NativeImagePixelFormat::rgba32_float: return 16;
  }
  return 0;
}

#if defined(_WIN32)
std::unique_ptr<NativeImageCodec> create_windows_wic_image_codec() noexcept;
#endif
#if defined(__ANDROID__)
std::unique_ptr<NativeImageCodec> create_android_image_codec() noexcept;
#endif
#if defined(__APPLE__)
std::unique_ptr<NativeImageCodec> create_apple_imageio_codec() noexcept;
#endif

}  // namespace

std::unique_ptr<NativeImageCodec>
create_native_image_codec(NativeStillPlatform platform) noexcept {
  switch (platform) {
    case NativeStillPlatform::windows:
#if defined(_WIN32)
      return create_windows_wic_image_codec();
#else
      return {};
#endif
    case NativeStillPlatform::android:
#if defined(__ANDROID__)
      return create_android_image_codec();
#else
      return {};
#endif
    case NativeStillPlatform::apple:
#if defined(__APPLE__)
      return create_apple_imageio_codec();
#else
      return {};
#endif
  }
  return {};
}

bool validate_native_image_buffer(const NativeImageBuffer& image,
                                  const NativeStillImageLimits& limits,
                                  std::string& diagnostic) noexcept {
  const auto bpp = bytes_per_pixel(image.format);
  if (!image.width || !image.height || !bpp) {
    diagnostic = "image buffer dimensions or format are invalid";
    return false;
  }
  if (image.width > limits.max_dimension || image.height > limits.max_dimension) {
    diagnostic = "image buffer exceeds maximum dimension";
    return false;
  }
  if (image.width > std::numeric_limits<std::uint32_t>::max() / bpp ||
      image.row_bytes < image.width * bpp) {
    diagnostic = "image row stride is invalid";
    return false;
  }
  const auto required = static_cast<std::uint64_t>(image.row_bytes) * image.height;
  if (required > limits.max_decoded_bytes || required != image.pixels.size()) {
    diagnostic = "image storage exceeds limits or does not match its stride";
    return false;
  }
  diagnostic.clear();
  return true;
}

ImageIoResult apply_image_orientation(NativeImageBuffer& image,
                                      ImageOrientation orientation,
                                      const NativeStillImageProgress& progress) noexcept {
  if (orientation == ImageOrientation::normal) return {};
  if (cancelled(progress))
    return {DIGITOR_RESULT_RESOURCE_IN_USE, "image orientation cancelled"};
  const auto bpp = bytes_per_pixel(image.format);
  if (!bpp) return {DIGITOR_RESULT_INVALID_ARGUMENT, "unsupported image format"};

  const bool swap_axes = orientation == ImageOrientation::mirror_horizontal_rotate_270 ||
                         orientation == ImageOrientation::rotate_90 ||
                         orientation == ImageOrientation::mirror_horizontal_rotate_90 ||
                         orientation == ImageOrientation::rotate_270;
  const auto out_width = swap_axes ? image.height : image.width;
  const auto out_height = swap_axes ? image.width : image.height;
  const auto out_row = out_width * bpp;
  if (static_cast<std::uint64_t>(out_row) * out_height >
      std::numeric_limits<std::size_t>::max())
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "oriented image is too large"};

  NativeImageBuffer transformed;
  transformed.width = out_width;
  transformed.height = out_height;
  transformed.row_bytes = out_row;
  transformed.format = image.format;
  transformed.premultiplied_alpha = image.premultiplied_alpha;
  try {
    transformed.pixels.resize(static_cast<std::size_t>(out_row) * out_height);
    shared_cpu_executor().parallel_for(out_height, 32, [&](std::size_t y0, std::size_t y1) {
      for (std::size_t y = y0; y < y1; ++y) {
        if (cancelled(progress)) continue;
        for (std::uint32_t x = 0; x < out_width; ++x) {
          std::uint32_t sx{}, sy{};
          switch (orientation) {
            case ImageOrientation::mirror_horizontal: sx = image.width - 1 - x; sy = static_cast<std::uint32_t>(y); break;
            case ImageOrientation::rotate_180: sx = image.width - 1 - x; sy = image.height - 1 - static_cast<std::uint32_t>(y); break;
            case ImageOrientation::mirror_vertical: sx = x; sy = image.height - 1 - static_cast<std::uint32_t>(y); break;
            case ImageOrientation::mirror_horizontal_rotate_270: sx = static_cast<std::uint32_t>(y); sy = x; break;
            case ImageOrientation::rotate_90: sx = static_cast<std::uint32_t>(y); sy = image.height - 1 - x; break;
            case ImageOrientation::mirror_horizontal_rotate_90: sx = image.width - 1 - static_cast<std::uint32_t>(y); sy = image.height - 1 - x; break;
            case ImageOrientation::rotate_270: sx = image.width - 1 - static_cast<std::uint32_t>(y); sy = x; break;
            case ImageOrientation::normal: sx = x; sy = static_cast<std::uint32_t>(y); break;
          }
          std::memcpy(transformed.pixels.data() + y * out_row + x * bpp,
                      image.pixels.data() + static_cast<std::size_t>(sy) * image.row_bytes + sx * bpp,
                      bpp);
        }
      }
    });
  } catch (const std::bad_alloc&) {
    return {DIGITOR_RESULT_OUT_OF_MEMORY, "orientation allocation failed"};
  } catch (...) {
    return {DIGITOR_RESULT_INTERNAL_ERROR, "orientation processing failed"};
  }
  if (cancelled(progress))
    return {DIGITOR_RESULT_RESOURCE_IN_USE, "image orientation cancelled"};
  image = std::move(transformed);
  report(progress, 0.15F);
  return {};
}

ImageIoResult prepare_image_for_export(NativeImageBuffer& image,
                                       const NativeImageEncodeRequest& request) noexcept {
  if (image.format != NativeImagePixelFormat::rgba8 &&
      image.format != NativeImagePixelFormat::bgra8)
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "export preparation currently requires an 8-bit RGBA/BGRA buffer"};
  if (cancelled(request.progress))
    return {DIGITOR_RESULT_RESOURCE_IN_USE, "image export cancelled"};

  const bool jpeg = request.options.format == ImageExportFormat::jpeg;
  const auto background = request.jpeg_background_rgba;
  const std::uint8_t br = static_cast<std::uint8_t>((background >> 24) & 0xffU);
  const std::uint8_t bg = static_cast<std::uint8_t>((background >> 16) & 0xffU);
  const std::uint8_t bb = static_cast<std::uint8_t>((background >> 8) & 0xffU);
  try {
    shared_cpu_executor().parallel_for(image.height, 32, [&](std::size_t y0, std::size_t y1) {
      for (std::size_t y = y0; y < y1; ++y) {
        auto* row = image.pixels.data() + y * image.row_bytes;
        for (std::uint32_t x = 0; x < image.width; ++x) {
          auto* p = row + x * 4U;
          auto& r = image.format == NativeImagePixelFormat::rgba8 ? p[0] : p[2];
          auto& g = p[1];
          auto& b = image.format == NativeImagePixelFormat::rgba8 ? p[2] : p[0];
          auto& a = p[3];
          if (jpeg) {
            const auto alpha = static_cast<std::uint32_t>(a);
            r = static_cast<std::uint8_t>((r * alpha + br * (255U - alpha) + 127U) / 255U);
            g = static_cast<std::uint8_t>((g * alpha + bg * (255U - alpha) + 127U) / 255U);
            b = static_cast<std::uint8_t>((b * alpha + bb * (255U - alpha) + 127U) / 255U);
            a = 255U;
          } else if (image.premultiplied_alpha && a) {
            r = static_cast<std::uint8_t>(std::min(255U, (static_cast<std::uint32_t>(r) * 255U + a / 2U) / a));
            g = static_cast<std::uint8_t>(std::min(255U, (static_cast<std::uint32_t>(g) * 255U + a / 2U) / a));
            b = static_cast<std::uint8_t>(std::min(255U, (static_cast<std::uint32_t>(b) * 255U + a / 2U) / a));
          }
        }
      }
    });
  } catch (...) {
    return {DIGITOR_RESULT_INTERNAL_ERROR, "image export preparation failed"};
  }
  image.premultiplied_alpha = false;
  report(request.progress, 0.90F);
  return {};
}

}  // namespace digitor
