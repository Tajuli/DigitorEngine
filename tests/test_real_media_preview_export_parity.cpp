#include "digitor/media.hpp"
#include "digitor/primary_wheels.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/qualifier.hpp"
#include "digitor/renderer.hpp"
#include "gpu/gpu_backend.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const std::string& message) {
  std::cerr << "PREVIEW_EXPORT_PARITY=FAIL diagnostic=\"" << message << "\"\n";
  return 1;
}

struct ChainEvidence {
  digitor::ProcessedGpuFramePtr final_frame;
  std::uint64_t fallback_dispatches{};
  std::uint64_t intermediate_readbacks{};
  std::uint64_t intermediate_reuploads{};
};

bool run_chain(digitor::IRenderBackend& backend,
               const digitor::VideoFrame& source,
               std::int64_t timestamp,
               ChainEvidence& out) {
  digitor::PrimaryWheelsDescriptor primary_desc;
  primary_desc.lift = {0.025F, -0.015F, 0.01F};
  primary_desc.gamma = {0.96F, 1.04F, 1.02F};
  primary_desc.gain = {1.05F, 0.98F, 1.03F};
  primary_desc.offset_master = 0.008F;
  const auto primary = digitor::PrimaryWheelsParameters::create(primary_desc);

  digitor::LogWheelsDescriptor log_desc;
  log_desc.shadows.rgb = {0.015F, -0.01F, 0.005F};
  log_desc.midtones.rgb = {-0.005F, 0.012F, 0.008F};
  log_desc.highlights.rgb = {0.01F, 0.004F, -0.008F};
  const auto log = digitor::LogWheelsParameters::create(log_desc);

  digitor::RgbCurvesParameters curve_desc;
  curve_desc.master.points = {{0.0F, 0.0F}, {0.25F, 0.22F}, {0.72F, 0.78F}, {1.0F, 1.0F}};
  curve_desc.red.points = {{0.0F, 0.0F}, {0.5F, 0.53F}, {1.0F, 1.0F}};
  const auto curves = digitor::CompiledRgbCurves::compile(curve_desc);

  digitor::QualifierSettings qualifier_settings;
  qualifier_settings.hue = {0.0F, 1.0F, 0.02F};
  qualifier_settings.saturation = {0.02F, 1.0F, 0.02F};
  qualifier_settings.luminance = {0.01F, 1.0F, 0.02F};
  const auto qualifier = digitor::HslQualifierParameters::create(qualifier_settings);

  digitor::ProcessedGpuFramePtr primary_frame;
  digitor::ProcessedGpuFramePtr log_frame;
  digitor::ProcessedGpuFramePtr curve_frame;
  digitor::ProcessedGpuFramePtr qualifier_frame;

  if (backend.process_primary_wheels_gpu(source.pixels, source.width, source.height,
                                         timestamp, *primary, primary_frame) != DIGITOR_RESULT_OK ||
      !primary_frame) return false;
  auto provenance = backend.execution_provenance();
  out.fallback_dispatches += provenance.primary_wheels_fallback_invocations;
  out.intermediate_readbacks += provenance.intermediate_readback_count;
  out.intermediate_reuploads += provenance.intermediate_reupload_count;

  if (backend.process_log_wheels_gpu(backend.gpu_source(primary_frame), timestamp,
                                     *log, log_frame) != DIGITOR_RESULT_OK || !log_frame) return false;
  provenance = backend.execution_provenance();
  out.fallback_dispatches += provenance.log_wheels_fallback_invocations;
  out.intermediate_readbacks += provenance.intermediate_readback_count;
  out.intermediate_reuploads += provenance.intermediate_reupload_count;

  if (backend.process_curves_gpu(backend.gpu_source(log_frame), timestamp,
                                 *curves, curve_frame) != DIGITOR_RESULT_OK || !curve_frame) return false;
  provenance = backend.execution_provenance();
  out.fallback_dispatches += provenance.curve_fallback_invocations;
  out.intermediate_readbacks += provenance.intermediate_readback_count;
  out.intermediate_reuploads += provenance.intermediate_reupload_count;

  if (backend.process_hsl_qualifier_gpu(backend.gpu_source(curve_frame), timestamp,
                                        *qualifier, qualifier_frame) != DIGITOR_RESULT_OK ||
      !qualifier_frame) return false;
  provenance = backend.execution_provenance();
  out.fallback_dispatches += provenance.cpu_fallback_invocations;
  out.intermediate_readbacks += provenance.intermediate_readback_count;
  out.intermediate_reuploads += provenance.intermediate_reupload_count;
  out.final_frame = std::move(qualifier_frame);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("expected real-media fixture path");
  if (!digitor::ffmpeg_available()) return fail("FFmpeg support unavailable");

  digitor::DecoderOptions decoder_options;
  decoder_options.hardware = digitor::HardwareDecode::cpu;
  decoder_options.allow_cpu_fallback = true;
  auto decoder = digitor::open_video_decoder(argv[1], decoder_options);
  if (!decoder) return fail("decoder creation failed");
  const auto decoded = decoder->decode(17);
  if (!decoded || !decoded->cpu_resident() || decoded->pixels.empty())
    return fail("real-media frame decode failed");

#if defined(_WIN32)
  constexpr DigitorRendererBackend requested_backend = DIGITOR_RENDERER_VULKAN;
  constexpr const char* backend_name = "Vulkan";
#elif defined(__APPLE__)
  constexpr DigitorRendererBackend requested_backend = DIGITOR_RENDERER_METAL;
  constexpr const char* backend_name = "Metal";
#elif defined(__ANDROID__)
  constexpr DigitorRendererBackend requested_backend = DIGITOR_RENDERER_VULKAN;
  constexpr const char* backend_name = "Vulkan";
#else
  std::cerr << "QUALIFICATION SKIP reason=no physical GPU backend for this host\n";
  return 77;
#endif

  auto backend = digitor::create_native_backend(requested_backend);
  if (!backend || !backend->initialize(true))
    return fail(std::string("physical ") + backend_name + " backend unavailable");
  const auto info = backend->info();
  if (!info.is_gpu || info.backend != requested_backend)
    return fail("selected backend is not the requested hardware GPU backend");

  const auto primary_before = digitor::primary_wheels_reference_count();
  const auto log_before = digitor::log_wheels_reference_count();
  const auto curves_before = digitor::cpu_curve_reference_count();
  const auto qualifier_before = digitor::hsl_qualifier_reference_count();

  ChainEvidence preview;
  ChainEvidence export_frame;
  if (!run_chain(*backend, *decoded, decoded->pts, preview))
    return fail("preview GPU chain failed");
  if (backend->present_gpu_frame(preview.final_frame) != DIGITOR_RESULT_OK)
    return fail("preview presentation failed");
  const auto preview_provenance = backend->execution_provenance();
  if (!run_chain(*backend, *decoded, decoded->pts, export_frame))
    return fail("export pre-encode GPU chain failed");

  std::vector<digitor::Color> preview_pixels(decoded->pixels.size());
  std::vector<digitor::Color> export_pixels(decoded->pixels.size());
  if (backend->validation_readback_final_frame(preview.final_frame, preview_pixels) != DIGITOR_RESULT_OK)
    return fail("preview validation readback failed");
  if (backend->validation_readback_final_frame(export_frame.final_frame, export_pixels) != DIGITOR_RESULT_OK)
    return fail("export validation readback failed");

  digitor::VideoFrame preview_cpu = *decoded;
  digitor::VideoFrame export_cpu = *decoded;
  preview_cpu.pixels = std::move(preview_pixels);
  export_cpu.pixels = std::move(export_pixels);
  const auto parity = digitor::qualify_preview_export_hashes(preview_cpu, export_cpu, 90.0, 0.999999);

  const auto cpu_invocations =
      (digitor::primary_wheels_reference_count() - primary_before) +
      (digitor::log_wheels_reference_count() - log_before) +
      (digitor::cpu_curve_reference_count() - curves_before) +
      (digitor::hsl_qualifier_reference_count() - qualifier_before);
  const auto fallback_dispatches = preview.fallback_dispatches + export_frame.fallback_dispatches;
  const auto intermediate_readbacks = preview.intermediate_readbacks + export_frame.intermediate_readbacks;
  const auto intermediate_reuploads = preview.intermediate_reuploads + export_frame.intermediate_reuploads;

  std::cout << "PREVIEW_EXPORT_PARITY backend=" << backend_name
            << " device=\"" << info.device_name << "\""
            << " width=" << decoded->width << " height=" << decoded->height
            << " preview_hash=" << parity.preview_hash
            << " export_hash=" << parity.export_hash
            << " max_absolute_error=" << parity.pixels.max_absolute_error
            << " rms=" << parity.pixels.rms_error
            << " psnr=" << parity.pixels.psnr
            << " ssim=" << parity.pixels.ssim
            << " differing_pixels=" << parity.pixels.differing_pixels
            << " cpu_invocations=" << cpu_invocations
            << " fallback_dispatches=" << fallback_dispatches
            << " intermediate_readbacks=" << intermediate_readbacks
            << " intermediate_reuploads=" << intermediate_reuploads
            << " normal_preview_readbacks=" << preview_provenance.normal_preview_readback_count
            << '\n';

  const bool passed = parity.passed && preview.final_frame && export_frame.final_frame &&
                      preview.final_frame->identity() != export_frame.final_frame->identity() &&
                      preview_provenance.preview_source == digitor::PreviewSource::gpu &&
                      preview_provenance.direct_preview_consumed &&
                      preview_provenance.normal_preview_readback_count == 0 &&
                      cpu_invocations == 0 && fallback_dispatches == 0 &&
                      intermediate_readbacks == 0 && intermediate_reuploads == 0;
  backend->shutdown();
  if (!passed) return fail("preview/export GPU parity or zero-fallback contract failed");
  std::cout << "REAL_MEDIA_PREVIEW_EXPORT_PARITY=PASS\n";
  return 0;
}
