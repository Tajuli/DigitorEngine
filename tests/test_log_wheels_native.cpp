#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "digitor/log_wheels.hpp"
#include "digitor/renderer.hpp"
#include "gpu/gpu_backend.hpp"

namespace {

using digitor::Color;
using digitor::GpuFailurePoint;
using digitor::IRenderBackend;
using digitor::ProcessedGpuFramePtr;

constexpr std::uint32_t kWidth = 7;
constexpr std::uint32_t kHeight = 5;
constexpr std::size_t kPixelCount = std::size_t{kWidth} * kHeight;

std::vector<Color> make_input() {
  std::vector<Color> input(kPixelCount);
  std::uint32_t state = 0x4c4f4757u;
  for (std::size_t i = 0; i < input.size(); ++i) {
    state = state * 1664525u + 1013904223u;
    const auto channel = [&](unsigned shift) {
      return float((state >> shift) & 0xffu) / 127.5f - 0.5f;
    };
    input[i] = {channel(0), channel(8), channel(16),
                0.15f + float(i % 7) / 10.0f};
  }
  input[0] = {0.0f, 0.0f, 0.0f, 0.31f};
  input[1] = {0.18f, 0.12f, 0.08f, 0.42f};
  input[2] = {0.50f, 0.50f, 0.50f, 0.53f};
  input[3] = {0.88f, 0.92f, 1.10f, 0.64f};
  input[4] = {-0.75f, -0.10f, -2.0f, 0.75f};
  input[5] = {1.50f, 4.00f, 12.0f, 0.86f};
  return input;
}

std::shared_ptr<const digitor::LogWheelsParameters> make_parameters() {
  digitor::LogWheelsDescriptor d;
  d.shadows.rgb = {0.035f, -0.018f, 0.012f};
  d.shadows.master = 0.10f;
  d.midtones.rgb = {-0.012f, 0.028f, 0.010f};
  d.midtones.master = -0.07f;
  d.highlights.rgb = {0.014f, -0.006f, 0.032f};
  d.highlights.master = 0.08f;
  d.global.rgb = {0.006f, 0.002f, -0.004f};
  d.global.master = 0.04f;
  d.shadow_pivot = 0.23f;
  d.highlight_pivot = 0.77f;
  d.transition_width = 0.18f;
  return digitor::LogWheelsParameters::create(d);
}

struct Metrics {
  double maximum{};
  double relative{};
  double rms{};
  double psnr{};
  double ssim{};
  std::size_t failures{};
};

Metrics compare_pixels(std::span<const Color> expected,
                       std::span<const Color> actual) {
  Metrics m;
  if (expected.size() != actual.size()) {
    m.failures = std::max(expected.size(), actual.size());
    return m;
  }
  double squares = 0.0;
  for (std::size_t n = 0; n < expected.size(); ++n) {
    for (int c = 0; c < 4; ++c) {
      const auto e = (&expected[n].r)[c];
      const auto a = (&actual[n].r)[c];
      const double error = std::abs(double(e) - double(a));
      const double relative =
          error / std::max(1.0e-6, std::abs(double(e)));
      m.maximum = std::max(m.maximum, error);
      m.relative = std::max(m.relative, relative);
      squares += error * error;
      if (error > 2.0e-5 && relative > 2.0e-5) ++m.failures;
    }
  }
  m.rms = std::sqrt(squares / double(expected.size() * 4));
  m.psnr = m.rms == 0.0 ? INFINITY : 20.0 * std::log10(1.0 / m.rms);
  digitor::VideoFrame e{.width = kWidth, .height = kHeight,
                        .pixels = std::vector<Color>(expected.begin(), expected.end())};
  digitor::VideoFrame a{.width = kWidth, .height = kHeight,
                        .pixels = std::vector<Color>(actual.begin(), actual.end())};
  m.ssim = digitor::calculate_ssim(e, a);
  return m;
}

bool exact_equal(std::span<const Color> a, std::span<const Color> b) {
  return a.size() == b.size() &&
         std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

bool qualify_numerical(IRenderBackend& backend, std::string_view name) {
  const auto input = make_input();
  const auto parameters = make_parameters();
  std::vector<Color> expected(input.size());
  digitor::apply_log_wheels_reference(input, expected, *parameters);

  const auto reference_after_expected = digitor::log_wheels_reference_count();
  ProcessedGpuFramePtr frame;
  const auto process = backend.process_log_wheels_gpu(
      input, kWidth, kHeight, 4901, *parameters, frame);
  const auto process_evidence = backend.execution_provenance();
  const auto present = frame ? backend.present_gpu_frame(frame)
                             : DIGITOR_RESULT_INTERNAL_ERROR;
  const auto preview_evidence = backend.execution_provenance();
  std::vector<Color> actual(input.size());
  const auto readback = frame
      ? backend.validation_readback_log_wheels(frame, actual)
      : DIGITOR_RESULT_INTERNAL_ERROR;
  const auto validation_evidence = backend.execution_provenance();
  const auto metrics = compare_pixels(expected, actual);

  const bool provenance =
      process_evidence.gpu_execution &&
      process_evidence.log_wheels_enabled &&
      process_evidence.log_wheels_parameter_identity == parameters->identity() &&
      process_evidence.log_wheels_parameters_bound &&
      process_evidence.log_wheels_source_bound &&
      process_evidence.log_wheels_destination_bound &&
      process_evidence.cpu_log_wheels_invocations == 0 &&
      process_evidence.log_wheels_fallback_invocations == 0 &&
      process_evidence.normal_preview_readback_count == 0 &&
      preview_evidence.preview_source == digitor::PreviewSource::gpu &&
      preview_evidence.direct_preview_consumed &&
      preview_evidence.normal_preview_readback_count == 0 &&
      validation_evidence.validation_readback_completed &&
      validation_evidence.cpu_log_wheels_invocations == 0 &&
      validation_evidence.log_wheels_fallback_invocations == 0;

  const bool passed =
      process == DIGITOR_RESULT_OK && frame && frame->ready() &&
      frame->identity() != 0 && present == DIGITOR_RESULT_OK &&
      readback == DIGITOR_RESULT_OK &&
      digitor::log_wheels_reference_count() == reference_after_expected &&
      provenance && metrics.failures == 0 && metrics.rms <= 2.0e-4 &&
      metrics.ssim >= 0.999;

  std::cerr << "LOG_WHEELS_METRICS backend=" << name
            << " device=\"" << backend.info().device_name << "\""
            << " source=cpu"
            << " max_absolute_error=" << metrics.maximum
            << " max_relative_error=" << metrics.relative
            << " rms=" << metrics.rms
            << " psnr=" << metrics.psnr
            << " ssim=" << metrics.ssim
            << " failing_components=" << metrics.failures
            << " cpu_delta=" << process_evidence.cpu_log_wheels_invocations
            << " fallback=" << process_evidence.log_wheels_fallback_invocations
            << " normal_readback=" << process_evidence.normal_preview_readback_count
            << " direct_preview=" << preview_evidence.direct_preview_consumed
            << " validation_readback="
            << validation_evidence.validation_readback_completed
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool qualify_gpu_source_chain(IRenderBackend& backend, std::string_view name) {
  const auto input = make_input();
  const auto parameters = make_parameters();

  std::vector<Color> once(input.size()), expected(input.size());
  digitor::apply_log_wheels_reference(input, once, *parameters);
  digitor::apply_log_wheels_reference(once, expected, *parameters);
  const auto reference_after_expected = digitor::log_wheels_reference_count();

  ProcessedGpuFramePtr first;
  const auto first_result = backend.process_log_wheels_gpu(
      input, kWidth, kHeight, 4910, *parameters, first);
  ProcessedGpuFramePtr second;
  const auto second_result =
      first_result == DIGITOR_RESULT_OK && first
          ? backend.process_log_wheels_gpu(
                backend.gpu_source(first), 4911, *parameters, second)
          : first_result;
  const auto chain_evidence = backend.execution_provenance();

  std::vector<Color> actual(input.size());
  const auto readback = second
      ? backend.validation_readback_log_wheels(second, actual)
      : DIGITOR_RESULT_INTERNAL_ERROR;
  const auto metrics = compare_pixels(expected, actual);

  const bool passed =
      first_result == DIGITOR_RESULT_OK && second_result == DIGITOR_RESULT_OK &&
      first && second && first->identity() != second->identity() &&
      readback == DIGITOR_RESULT_OK &&
      digitor::log_wheels_reference_count() == reference_after_expected &&
      chain_evidence.cpu_log_wheels_invocations == 0 &&
      chain_evidence.log_wheels_fallback_invocations == 0 &&
      chain_evidence.intermediate_readback_count == 0 &&
      chain_evidence.intermediate_reupload_count == 0 &&
      chain_evidence.normal_preview_readback_count == 0 &&
      metrics.failures == 0 && metrics.rms <= 2.0e-4 &&
      metrics.ssim >= 0.999;

  std::cerr << "LOG_WHEELS_CHAIN backend=" << name
            << " source=gpu"
            << " intermediate_readback=" << chain_evidence.intermediate_readback_count
            << " intermediate_reupload=" << chain_evidence.intermediate_reupload_count
            << " cpu_delta=" << chain_evidence.cpu_log_wheels_invocations
            << " fallback=" << chain_evidence.log_wheels_fallback_invocations
            << " rms=" << metrics.rms
            << " ssim=" << metrics.ssim
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool qualify_determinism_cache_cleanup(IRenderBackend& backend,
                                       std::string_view name) {
  const auto input = make_input();
  const auto parameters = make_parameters();

  backend.clear_native_pipeline_cache_for_test();
  const auto baseline_resources = backend.native_resource_counts();
  const auto before_cache = backend.native_pipeline_cache_counters();

  std::array<std::vector<Color>, 3> outputs;
  bool operations_ok = true;
  for (std::size_t run = 0; run < outputs.size(); ++run) {
    ProcessedGpuFramePtr frame;
    operations_ok &= backend.process_log_wheels_gpu(
                         input, kWidth, kHeight, 4920 + std::int64_t(run),
                         *parameters, frame) == DIGITOR_RESULT_OK &&
                     static_cast<bool>(frame);
    outputs[run].resize(input.size());
    operations_ok &=
        frame &&
        backend.validation_readback_log_wheels(frame, outputs[run]) ==
            DIGITOR_RESULT_OK;
    frame.reset();
  }

  const auto after_cache = backend.native_pipeline_cache_counters();
  const auto repeat01 = compare_pixels(outputs[0], outputs[1]);
  const auto repeat02 = compare_pixels(outputs[0], outputs[2]);
  const bool deterministic = repeat01.maximum <= 1.0e-7 &&
                             repeat02.maximum <= 1.0e-7;
  const bool cache_reused =
      after_cache.lookups >= before_cache.lookups + 3 &&
      after_cache.misses >= before_cache.misses + 1 &&
      after_cache.hits >= before_cache.hits + 1 &&
      after_cache.creations >= before_cache.creations + 1;

  backend.clear_native_pipeline_cache_for_test();
  const bool cleanup = backend.native_resource_counts() == baseline_resources;
  const auto evidence = backend.execution_provenance();
  const bool passed =
      operations_ok && deterministic && cache_reused && cleanup &&
      evidence.cpu_log_wheels_invocations == 0 &&
      evidence.log_wheels_fallback_invocations == 0;

  std::cerr << "LOG_WHEELS_DETERMINISM backend=" << name
            << " exact_repeat=" << deterministic
            << " cache_misses=" << (after_cache.misses - before_cache.misses)
            << " cache_hits=" << (after_cache.hits - before_cache.hits)
            << " cache_creations="
            << (after_cache.creations - before_cache.creations)
            << " cleanup=" << cleanup
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

std::vector<GpuFailurePoint> process_failure_points(
    DigitorRendererBackend backend) {
  using F = GpuFailurePoint;
  switch (backend) {
    case DIGITOR_RENDERER_VULKAN:
    case DIGITOR_RENDERER_D3D12:
    case DIGITOR_RENDERER_METAL:
      return {F::PipelineCreation, F::ProcessedFrameCreation};
    case DIGITOR_RENDERER_OPENGL_ES:
      return {F::ProgramLink, F::ProcessedFrameCreation};
    default:
      return {};
  }
}

std::vector<GpuFailurePoint> validation_failure_points(
    DigitorRendererBackend backend) {
  using F = GpuFailurePoint;
  switch (backend) {
    case DIGITOR_RENDERER_VULKAN:
    case DIGITOR_RENDERER_D3D12:
    case DIGITOR_RENDERER_METAL:
    case DIGITOR_RENDERER_OPENGL_ES:
      return {F::ValidationReadbackMap};
    default:
      return {};
  }
}

bool qualify_failure_matrix(IRenderBackend& backend, std::string_view name) {
  const auto input = make_input();
  const auto parameters = make_parameters();
  bool passed = true;

  for (const auto point : process_failure_points(backend.info().backend)) {
    backend.clear_native_pipeline_cache_for_test();
    ProcessedGpuFramePtr failed;
    digitor::set_gpu_failure_point(point);
    const auto failure = backend.process_log_wheels_gpu(
        input, kWidth, kHeight, 4930, *parameters, failed);
    const auto evidence = backend.execution_provenance();
    digitor::set_gpu_failure_point(GpuFailurePoint::None);

    ProcessedGpuFramePtr recovered;
    const auto recovery = backend.process_log_wheels_gpu(
        input, kWidth, kHeight, 4931, *parameters, recovered);
    const bool reached = evidence.requested_failure_point == point &&
                         evidence.actual_stage_reached == point;
    const bool ok = failure != DIGITOR_RESULT_OK && !failed && reached &&
                    evidence.cpu_log_wheels_invocations == 0 &&
                    evidence.log_wheels_fallback_invocations == 0 &&
                    recovery == DIGITOR_RESULT_OK && recovered;
    std::cerr << "LOG_WHEELS_FAILURE_STAGE backend=" << name
              << " path=process stage=" << digitor::gpu_failure_point_name(point)
              << " classification=" << (ok ? "PASS" : "FAIL")
              << " reached=" << reached
              << " recovery=" << (recovery == DIGITOR_RESULT_OK) << '\n';
    passed &= ok;
  }

  ProcessedGpuFramePtr frame;
  if (backend.process_log_wheels_gpu(input, kWidth, kHeight, 4940,
                                     *parameters, frame) != DIGITOR_RESULT_OK ||
      !frame)
    return false;
  for (const auto point : validation_failure_points(backend.info().backend)) {
    std::vector<Color> readback(input.size());
    digitor::set_gpu_failure_point(point);
    const auto failure = backend.validation_readback_log_wheels(frame, readback);
    const auto evidence = backend.execution_provenance();
    digitor::set_gpu_failure_point(GpuFailurePoint::None);
    const auto recovery = backend.validation_readback_log_wheels(frame, readback);
    const bool reached = evidence.requested_failure_point == point &&
                         evidence.actual_stage_reached == point;
    const bool ok = failure != DIGITOR_RESULT_OK && reached &&
                    evidence.cpu_log_wheels_invocations == 0 &&
                    evidence.log_wheels_fallback_invocations == 0 &&
                    recovery == DIGITOR_RESULT_OK;
    std::cerr << "LOG_WHEELS_FAILURE_STAGE backend=" << name
              << " path=validation stage=" << digitor::gpu_failure_point_name(point)
              << " classification=" << (ok ? "PASS" : "FAIL")
              << " reached=" << reached
              << " recovery=" << (recovery == DIGITOR_RESULT_OK) << '\n';
    passed &= ok;
  }
  digitor::set_gpu_failure_point(GpuFailurePoint::None);
  return passed;
}

bool qualify_cache_failure(IRenderBackend& backend, std::string_view name) {
  const auto input = make_input();
  const auto parameters = make_parameters();
  const auto point = backend.info().backend == DIGITOR_RENDERER_OPENGL_ES
                         ? GpuFailurePoint::ProgramLink
                         : GpuFailurePoint::PipelineCreation;
  backend.clear_native_pipeline_cache_for_test();
  const auto before = backend.native_pipeline_cache_counters();
  ProcessedGpuFramePtr failed_frame;
  digitor::set_gpu_failure_point(point);
  const auto failure = backend.process_log_wheels_gpu(
      input, kWidth, kHeight, 4950, *parameters, failed_frame);
  const auto failed = backend.native_pipeline_cache_counters();
  digitor::set_gpu_failure_point(GpuFailurePoint::None);

  ProcessedGpuFramePtr created_frame;
  const auto retry = backend.process_log_wheels_gpu(
      input, kWidth, kHeight, 4951, *parameters, created_frame);
  const auto created = backend.native_pipeline_cache_counters();
  ProcessedGpuFramePtr hit_frame;
  const auto hit = backend.process_log_wheels_gpu(
      input, kWidth, kHeight, 4952, *parameters, hit_frame);
  const auto reused = backend.native_pipeline_cache_counters();

  const bool rejected = failure != DIGITOR_RESULT_OK && !failed_frame &&
                        failed.creation_failures >= before.creation_failures + 1;
  const bool passed = rejected && retry == DIGITOR_RESULT_OK && created_frame &&
                      created.creations >= failed.creations + 1 &&
                      hit == DIGITOR_RESULT_OK && hit_frame &&
                      reused.hits >= created.hits + 1;
  std::cerr << "LOG_WHEELS_CACHE_FAILURE backend=" << name
            << " stage=" << digitor::gpu_failure_point_name(point)
            << " rejected=" << rejected
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool qualify_backend(DigitorRendererBackend kind, std::string_view name) {
  auto backend = digitor::create_native_backend(kind);
  if (!backend || !backend->initialize(true)) {
    std::cerr << "LOG_WHEELS_BACKEND_UNAVAILABLE backend=" << name << '\n';
#if defined(_WIN32)
    return kind != DIGITOR_RENDERER_D3D12;
#else
    return true;
#endif
  }

  digitor::reset_log_wheels_reference_count();
  bool passed = true;
  passed &= qualify_numerical(*backend, name);
  passed &= qualify_gpu_source_chain(*backend, name);
  passed &= qualify_determinism_cache_cleanup(*backend, name);
  passed &= qualify_cache_failure(*backend, name);
  passed &= qualify_failure_matrix(*backend, name);

  backend->shutdown();
  std::cerr << "LOG_WHEELS_BACKEND_RESULT backend=" << name
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

}  // namespace

int main() {
#if defined(_WIN32)
  constexpr std::array entries{
      std::pair{DIGITOR_RENDERER_D3D12, std::string_view{"Direct3D12"}},
      std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}}};
#elif defined(__APPLE__)
  constexpr std::array entries{
      std::pair{DIGITOR_RENDERER_METAL, std::string_view{"Metal"}}};
#elif defined(__ANDROID__)
  constexpr std::array entries{
      std::pair{DIGITOR_RENDERER_OPENGL_ES, std::string_view{"OpenGL ES"}},
      std::pair{DIGITOR_RENDERER_VULKAN, std::string_view{"Vulkan"}}};
#else
  std::cerr << "LOG_WHEELS_QUALIFICATION_SKIP "
               "reason=no-native-GPU-backend-for-this-host\n";
  return 77;
#endif

#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
  bool passed = true;
  unsigned executed = 0;
  for (const auto& [kind, name] : entries) {
    auto probe = digitor::create_native_backend(kind);
    if (!probe || !probe->initialize(true)) {
      std::cerr << "LOG_WHEELS_BACKEND_UNAVAILABLE backend=" << name << '\n';
#if defined(_WIN32)
      if (kind == DIGITOR_RENDERER_D3D12) passed = false;
#endif
      continue;
    }
    ++executed;
    probe->shutdown();
    passed &= qualify_backend(kind, name);
  }
  if (executed == 0) {
    std::cerr << "LOG_WHEELS_QUALIFICATION_SKIP reason=no-usable-GPU-device\n";
    return 77;
  }
  return passed ? 0 : 1;
#endif
}
