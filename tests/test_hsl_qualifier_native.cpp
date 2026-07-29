#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "digitor/qualifier.hpp"
#include "gpu/gpu_backend.hpp"

namespace {
using digitor::Color;
using digitor::GpuFailurePoint;
using digitor::IRenderBackend;
using digitor::ProcessedGpuFramePtr;

constexpr std::uint32_t width = 8, height = 6;
constexpr std::size_t count = std::size_t(width) * height;

std::vector<Color> input_pixels() {
  std::vector<Color> pixels(count);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    const float x = float(i % width) / float(width - 1);
    const float y = float(i / width) / float(height - 1);
    pixels[i] = {x, y, 1.0f - x, 0.2f + 0.8f * y};
  }
  pixels[0] = {1, 0, 0, .25f};
  pixels[1] = {0, 1, 0, .5f};
  pixels[2] = {0, 0, 1, .75f};
  pixels[3] = {.5f, .5f, .5f, 1};
  return pixels;
}

std::shared_ptr<const digitor::HslQualifierParameters> parameters() {
  digitor::QualifierSettings s;
  s.hue = {.92f, .10f, .06f};
  s.saturation = {.20f, 1.0f, .10f};
  s.luminance = {.04f, .96f, .05f};
  s.clean_black = .02f;
  s.clean_white = .02f;
  s.matte_output = true;
  return digitor::HslQualifierParameters::create(s);
}

struct Metrics { double maximum{}, rms{}; std::size_t failures{}; };
Metrics compare(std::span<const float> expected, std::span<const float> actual) {
  Metrics m;
  if (expected.size() != actual.size()) { m.failures = std::max(expected.size(), actual.size()); return m; }
  double squares{};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const double error = std::abs(double(expected[i]) - double(actual[i]));
    m.maximum = std::max(m.maximum, error);
    squares += error * error;
    if (error > 2.0e-4) ++m.failures;
  }
  m.rms = std::sqrt(squares / std::max<std::size_t>(1, expected.size()));
  return m;
}

bool numerical(IRenderBackend& backend, std::string_view name) {
  const auto input = input_pixels();
  const auto p = parameters();
  std::vector<float> expected(input.size());
  digitor::apply_hsl_qualifier_reference(input, expected, *p);
  const auto reference_count = digitor::hsl_qualifier_reference_count();

  ProcessedGpuFramePtr frame;
  const auto process = backend.process_hsl_qualifier_gpu(input, width, height, 5001, *p, frame);
  const auto process_evidence = backend.execution_provenance();
  std::vector<float> actual(input.size());
  const auto readback = frame ? backend.validation_readback_hsl_qualifier(frame, actual)
                              : DIGITOR_RESULT_INTERNAL_ERROR;
  const auto validation_evidence = backend.execution_provenance();
  const auto metrics = compare(expected, actual);
  const bool passed = process == DIGITOR_RESULT_OK && frame && frame->ready() &&
      readback == DIGITOR_RESULT_OK && metrics.failures == 0 && metrics.rms <= 2.0e-4 &&
      digitor::hsl_qualifier_reference_count() == reference_count &&
      process_evidence.gpu_execution && process_evidence.hsl_qualifier_enabled &&
      process_evidence.hsl_qualifier_parameter_identity == p->identity() &&
      process_evidence.hsl_qualifier_source_bound &&
      process_evidence.hsl_qualifier_destination_bound &&
      process_evidence.hsl_qualifier_parameters_bound &&
      process_evidence.normal_preview_readback_count == 0 &&
      validation_evidence.validation_readback_completed;
  std::cerr << "HSL_QUALIFIER_METRICS backend=" << name
            << " max=" << metrics.maximum << " rms=" << metrics.rms
            << " failures=" << metrics.failures
            << " normal_readback=" << process_evidence.normal_preview_readback_count
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool chain(IRenderBackend& backend, std::string_view name) {
  const auto input = input_pixels();
  const auto p = parameters();
  ProcessedGpuFramePtr first, second;
  const auto a = backend.process_hsl_qualifier_gpu(input, width, height, 5010, *p, first);
  const auto b = (a == DIGITOR_RESULT_OK && first)
      ? backend.process_hsl_qualifier_gpu(backend.gpu_source(first), 5011, *p, second)
      : a;
  const auto evidence = backend.execution_provenance();
  const bool passed = a == DIGITOR_RESULT_OK && b == DIGITOR_RESULT_OK && first && second &&
      first->identity() != second->identity() && evidence.intermediate_readback_count == 0 &&
      evidence.intermediate_reupload_count == 0 && evidence.normal_preview_readback_count == 0;
  std::cerr << "HSL_QUALIFIER_CHAIN backend=" << name
            << " intermediate_readback=" << evidence.intermediate_readback_count
            << " intermediate_reupload=" << evidence.intermediate_reupload_count
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool cache_and_determinism(IRenderBackend& backend, std::string_view name) {
  const auto input = input_pixels();
  const auto p = parameters();
  backend.clear_native_pipeline_cache_for_test();
  const auto before = backend.native_pipeline_cache_counters();
  std::array<std::vector<float>, 3> outputs;
  bool ok = true;
  for (int i = 0; i < 3; ++i) {
    ProcessedGpuFramePtr frame;
    ok &= backend.process_hsl_qualifier_gpu(input, width, height, 5020 + i, *p, frame) == DIGITOR_RESULT_OK && bool(frame);
    outputs[i].resize(input.size());
    ok &= frame && backend.validation_readback_hsl_qualifier(frame, outputs[i]) == DIGITOR_RESULT_OK;
  }
  const auto after = backend.native_pipeline_cache_counters();
  const bool deterministic = outputs[0] == outputs[1] && outputs[0] == outputs[2];
  const bool cache = after.creations >= before.creations + 1 && after.hits >= before.hits + 1;
  const bool passed = ok && deterministic && cache;
  std::cerr << "HSL_QUALIFIER_CACHE backend=" << name
            << " deterministic=" << deterministic << " cache_reused=" << cache
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool failure_recovery(IRenderBackend& backend, std::string_view name) {
  const auto input = input_pixels();
  const auto p = parameters();
  const auto stage = backend.info().backend == DIGITOR_RENDERER_OPENGL_ES
      ? GpuFailurePoint::ProgramLink : GpuFailurePoint::PipelineCreation;
  backend.clear_native_pipeline_cache_for_test();
  digitor::set_gpu_failure_point(stage);
  ProcessedGpuFramePtr failed;
  const auto failure = backend.process_hsl_qualifier_gpu(input, width, height, 5030, *p, failed);
  const auto evidence = backend.execution_provenance();
  digitor::set_gpu_failure_point(GpuFailurePoint::None);
  ProcessedGpuFramePtr recovered;
  const auto recovery = backend.process_hsl_qualifier_gpu(input, width, height, 5031, *p, recovered);
  const bool reached = evidence.requested_failure_point == stage && evidence.actual_stage_reached == stage;
  const bool passed = failure != DIGITOR_RESULT_OK && !failed && reached &&
      recovery == DIGITOR_RESULT_OK && recovered;
  std::cerr << "HSL_QUALIFIER_FAILURE backend=" << name
            << " stage=" << digitor::gpu_failure_point_name(stage)
            << " reached=" << reached << " recovery=" << (recovery == DIGITOR_RESULT_OK)
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}

bool qualify(DigitorRendererBackend kind, std::string_view name) {
  auto backend = digitor::create_native_backend(kind);
  if (!backend || !backend->initialize(true)) {
    std::cerr << "HSL_QUALIFIER_BACKEND_UNAVAILABLE backend=" << name << '\n';
    return true;
  }
  digitor::reset_hsl_qualifier_reference_count();
  bool passed = numerical(*backend, name);
  passed &= chain(*backend, name);
  passed &= cache_and_determinism(*backend, name);
  passed &= failure_recovery(*backend, name);
  backend->shutdown();
  std::cerr << "HSL_QUALIFIER_BACKEND_RESULT backend=" << name
            << " status=" << (passed ? "PASS" : "FAIL") << '\n';
  return passed;
}
} // namespace

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
  std::cerr << "HSL_QUALIFIER_QUALIFICATION_SKIP reason=no-native-GPU-backend-for-this-host\n";
  return 77;
#endif
#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
  bool passed = true;
  unsigned executed = 0;
  for (const auto& [kind, name] : entries) {
    auto probe = digitor::create_native_backend(kind);
    if (!probe || !probe->initialize(true)) continue;
    ++executed;
    probe->shutdown();
    passed &= qualify(kind, name);
  }
  if (!executed) return 77;
  return passed ? 0 : 1;
#endif
}
