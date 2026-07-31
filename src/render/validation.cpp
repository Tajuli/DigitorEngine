#include "core/numeric_utils.hpp"
#include "core/engine.hpp"
#include "digitor/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace digitor {
namespace {
void compatible(const VideoFrame &a, const VideoFrame &b) {
  if (a.width != b.width || a.height != b.height ||
      a.pixels.size() != b.pixels.size())
    throw std::invalid_argument("incompatible frames");
}
double channel(const Color &p, int c) {
  return c == 0 ? p.r : c == 1 ? p.g : c == 2 ? p.b : p.a;
}
} // namespace
double calculate_psnr(const VideoFrame &a, const VideoFrame &b) {
  compatible(a, b);
  if (a.pixels.empty())
    return std::numeric_limits<double>::infinity();
  double error = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i)
    for (int c = 0; c < 4; ++c) {
      const double d = channel(a.pixels[i], c) - channel(b.pixels[i], c);
      error += d * d;
    }
  error /= checked_size_to_double(a.pixels.size()) * 4.0;
  if (error == 0)
    return std::numeric_limits<double>::infinity();
  return 10 * std::log10(1 / error);
}
double calculate_ssim(const VideoFrame &a, const VideoFrame &b) {
  compatible(a, b);
  if (a.pixels.empty())
    return 1;
  const size_t n = a.pixels.size() * 3;
  double mx = 0, my = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i)
    for (int c = 0; c < 3; ++c) {
      mx += channel(a.pixels[i], c);
      my += channel(b.pixels[i], c);
    }
  const double sample_count = checked_size_to_double(n);
  mx /= sample_count;
  my /= sample_count;
  double vx = 0, vy = 0, cov = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i)
    for (int c = 0; c < 3; ++c) {
      double x = channel(a.pixels[i], c) - mx, y = channel(b.pixels[i], c) - my;
      vx += x * x;
      vy += y * y;
      cov += x * y;
    }
  const double divisor = checked_size_to_double(n > 1 ? n - 1 : 1);
  vx /= divisor;
  vy /= divisor;
  cov /= divisor;
  constexpr double c1 = .01 * .01, c2 = .03 * .03;
  return ((2 * mx * my + c1) * (2 * cov + c2)) /
         ((mx * mx + my * my + c1) * (vx + vy + c2));
}
PixelValidation validate_pixels(const VideoFrame &a, const VideoFrame &b,
                                double p, double s) {
  compatible(a, b);
  PixelValidation r;
  r.psnr = calculate_psnr(a, b);
  r.ssim = calculate_ssim(a, b);
  double squared = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i) {
    bool different = false;
    for (int c = 0; c < 4; ++c) {
      double error =
          std::abs(channel(a.pixels[i], c) - channel(b.pixels[i], c));
      r.max_absolute_error = std::max(r.max_absolute_error, error);
      squared += error * error;
      different |= error != 0;
    }
    r.differing_pixels += different;
  }
  if (!a.pixels.empty())
    r.rms_error =
        std::sqrt(squared / (checked_size_to_double(a.pixels.size()) * 4));
  r.passed = r.psnr >= p && r.ssim >= s;
  return r;
}
PixelValidation validate_preview_export(SharedRenderer &r,
                                        const RenderRequest &q, double p,
                                        double s) {
  if (Engine::instance().is_initialized() &&
      Engine::instance().renderer_info().is_gpu) {
    const auto preview_gpu = r.render_gpu_preview(q);
    if (!preview_gpu || !preview_gpu->ready())
      throw std::runtime_error("native GPU preview frame is not ready");
    const auto& metadata = preview_gpu->metadata();
    if (metadata.width != q.width || metadata.height != q.height ||
        metadata.timestamp != q.frame ||
        metadata.format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT)
      throw std::runtime_error("native GPU preview metadata violates parity contract");
    VideoFrame preview;
    preview.number = q.frame; preview.pts = q.frame;
    preview.width = q.width; preview.height = q.height;
    preview.pixel_format = DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;
    preview.pixels.resize(static_cast<std::size_t>(q.width) * q.height);
    if (Engine::instance().validation_readback_final_frame(preview_gpu, preview.pixels) !=
        DIGITOR_RESULT_OK)
      throw std::runtime_error("native GPU preview parity readback failed");

    // Render the export pre-encode frame independently. This must execute the
    // same graph recipe again rather than comparing a frame with itself.
    const auto export_preencode = r.render(q);
    return validate_pixels(preview, export_preencode, p, s);
  }
  // CPU-only builds retain a deterministic smoke check; production native
  // parity is exclusively established by the independent GPU path above.
  const auto preencode = r.render(q);
  return validate_pixels(preencode, preencode, p, s);
}
} // namespace digitor

#include <bit>
namespace digitor {
std::uint64_t deterministic_frame_hash(const VideoFrame& frame){
  std::uint64_t hash=1469598103934665603ull;
  auto mix=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){hash^=static_cast<unsigned char>((value>>(i*8))&0xffu);hash*=1099511628211ull;}};
  mix(static_cast<std::uint64_t>(frame.number));mix(frame.width);mix(frame.height);mix(static_cast<std::uint64_t>(frame.pixel_format));
  for(const auto& p:frame.pixels){mix(std::bit_cast<std::uint32_t>(p.r));mix(std::bit_cast<std::uint32_t>(p.g));mix(std::bit_cast<std::uint32_t>(p.b));mix(std::bit_cast<std::uint32_t>(p.a));}
  return hash;
}
PreviewExportHashQualification qualify_preview_export_hashes(const VideoFrame& preview,const VideoFrame& export_frame,double minimum_psnr,double minimum_ssim){
  PreviewExportHashQualification result;
  result.preview_hash=deterministic_frame_hash(preview);result.export_hash=deterministic_frame_hash(export_frame);
  result.pixels=validate_pixels(preview,export_frame,minimum_psnr,minimum_ssim);
  result.passed=result.pixels.passed&&(result.preview_hash==result.export_hash||result.pixels.max_absolute_error<=1.0/255.0);
  return result;
}
} // namespace digitor
