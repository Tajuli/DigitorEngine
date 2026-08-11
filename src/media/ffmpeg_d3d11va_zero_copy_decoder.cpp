#include "digitor/ffmpeg_d3d11va_zero_copy_decoder.hpp"

#include <new>
#include <utility>

namespace digitor {

FfmpegD3D11vaZeroCopyDecoder::FfmpegD3D11vaZeroCopyDecoder(
    void* d3d12_device,
    FfmpegD3D11vaZeroCopyOptions options,
    LegacyCpuFallbackCallback legacy,
    const void* frame_context_identity)
    : options_(options), legacy_(std::move(legacy)),
      converter_(std::make_unique<WindowsD3D12YuvConverter>(
          d3d12_device, frame_context_identity)),
      importer_(std::make_unique<WindowsD3D12ZeroCopyImporter>(
          d3d12_device, converter_->callback())) {
  if (options_.fallback ==
          ZeroCopyFallbackPolicy::allow_explicit_legacy_fallback &&
      !legacy_) {
    throw std::invalid_argument(
        "explicit legacy fallback policy requires a fallback callback");
  }
}

FfmpegD3D11vaZeroCopyDecoder::~FfmpegD3D11vaZeroCopyDecoder() = default;

DigitorResult FfmpegD3D11vaZeroCopyDecoder::process(
    void* av_frame, std::int64_t timestamp_us,
    FfmpegD3D11vaZeroCopyResult& out) noexcept {
  out = {};
  out.zero_copy_attempted = true;
  out.legacy_fallback_allowed =
      options_.fallback ==
      ZeroCopyFallbackPolicy::allow_explicit_legacy_fallback;

  auto fallback = [&](DigitorResult zero_copy_result,
                      const std::string& diagnostic) noexcept {
    out.diagnostic = diagnostic;
    if (!out.legacy_fallback_allowed || !legacy_) {
      return zero_copy_result;
    }
    out.legacy_fallback_requested = true;
    try {
      ProcessedGpuFramePtr legacy_frame;
      const auto result = legacy_(av_frame, timestamp_us, legacy_frame);
      if (result == DIGITOR_RESULT_OK && legacy_frame) {
        out.frame = std::move(legacy_frame);
        out.diagnostic =
            "zero-copy unavailable; explicit legacy fallback completed";
        return DIGITOR_RESULT_OK;
      }
      out.diagnostic =
          "zero-copy unavailable and explicit legacy fallback failed";
      return result == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR
                                         : result;
    } catch (const std::bad_alloc&) {
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  };

  if (!av_frame || timestamp_us < 0) {
    return fallback(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "invalid D3D11VA frame or timestamp");
  }

  try {
    auto extract_result = extract_ffmpeg_d3d11va_surface(
        av_frame, timestamp_us, out.extraction);
    if (extract_result != DIGITOR_RESULT_OK) {
      return fallback(extract_result, out.extraction.diagnostic);
    }

    if (options_.require_p010_for_10bit &&
        out.extraction.surface.lifetime &&
        out.extraction.surface.lifetime->descriptor().pixel_format ==
            NativeMediaPixelFormat::yuv420p10 &&
        out.extraction.surface.format != WindowsZeroCopyFormat::p010) {
      return fallback(DIGITOR_RESULT_UNSUPPORTED,
                      "10-bit decoder frame did not preserve P010 storage");
    }

    ProcessedGpuFramePtr frame;
    const auto import_result = importer_->import(
        out.extraction.surface, frame, &out.import);
    if (import_result != DIGITOR_RESULT_OK || !frame) {
      return fallback(import_result,
                      out.import.diagnostic.empty()
                          ? "D3D12 zero-copy import failed"
                          : out.import.diagnostic);
    }

    if (frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
        frame->metadata().timestamp != timestamp_us ||
        frame->metadata().width != out.extraction.surface.width ||
        frame->metadata().height != out.extraction.surface.height) {
      return fallback(DIGITOR_RESULT_INTERNAL_ERROR,
                      "zero-copy frame violated RGBA16F metadata contract");
    }

    out.frame = std::move(frame);
    out.zero_copy_succeeded = true;
    out.diagnostic.clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return fallback(DIGITOR_RESULT_OUT_OF_MEMORY,
                    "out of memory in zero-copy decoder wrapper");
  } catch (...) {
    return fallback(DIGITOR_RESULT_INTERNAL_ERROR,
                    "unexpected zero-copy decoder failure");
  }
}

} // namespace digitor
