#include "digitor/timeline_render_runtime.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

digitor::RenderVideoFrame make_gpu_frame(std::uint64_t identity,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         const char* provenance,
                                         const void* supplied_context = nullptr) {
  using namespace digitor;
  static int context;
  GpuFrameMetadata metadata;
  metadata.width = width;
  metadata.height = height;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;
  metadata.alpha = GpuFrameAlpha::premultiplied;
  metadata.color_metadata = "scene-linear-rec2020";
  auto native = std::shared_ptr<void>(new std::uint64_t(identity), [](void* value) {
    delete static_cast<std::uint64_t*>(value);
  });
  auto ready = std::make_shared<std::atomic_bool>(true);
  RenderVideoFrame frame;
  frame.width = width;
  frame.height = height;
  frame.gpu = std::make_shared<ProcessedGpuFrame>(
      supplied_context ? supplied_context : &context, DIGITOR_RENDERER_D3D12,
      metadata, identity, std::move(native), std::move(ready), false);
  frame.provenance = provenance;
  return frame;
}
}  // namespace

int main() {
  using namespace digitor;
  TimelineProjectModel project;
  project.tracks = {
      {"v1", "Video 1", TimelineTrackType::video, false, false, false,
       {{"base", TimelineClipType::video, 0, 2'000'000, 100'000, 4'000'000, {}, {}, false, true, false, 1.0, false}}},
      {"v2", "Overlay", TimelineTrackType::video, false, false, false,
       {{"overlay", TimelineClipType::overlay, 0, 2'000'000, 0, 2'000'000, {}, {}, false, true, false, 1.0, false}}},
      {"a1", "Audio", TimelineTrackType::audio, false, false, false,
       {{"audio", TimelineClipType::audio, 0, 2'000'000, 0, 2'000'000, {}, {}, false, true, false, 0.5, false}}}};

  std::unordered_map<std::string, ClipExecutionOverrides> overrides;
  overrides["overlay"].opacity = 0.5;
  TimelineRenderExecutor executor(project, overrides);
  require(executor.preview_export_plan_equivalent(500'000, 2, 1, 1, 1),
          "preview/export scheduling plan differs");

  int decode_calls = 0;
  TimelineRenderCallbacks callbacks;
  callbacks.decode_video = [&](const VideoExecutionLayer& layer, bool) {
    ++decode_calls;
    RenderVideoFrame frame;
    frame.width = 2;
    frame.height = 1;
    frame.rgba.assign(8, layer.clip_id == "base" ? 0.2F : 0.4F);
    frame.provenance = "source:" + layer.clip_id;
    return std::optional<RenderVideoFrame>{std::move(frame)};
  };
  callbacks.apply_effects = [](const VideoExecutionLayer&, RenderVideoFrame& frame) {
    for (auto& value : frame.rgba) value += 0.1F;
    return true;
  };
  callbacks.composite = [](const VideoExecutionLayer&, const RenderVideoFrame& input,
                           RenderVideoFrame& output) {
    for (std::size_t i = 0; i < output.rgba.size(); ++i) output.rgba[i] += input.rgba[i];
    output.provenance += "+" + input.provenance;
    return true;
  };
  callbacks.decode_audio = [](const AudioExecutionLayer&, std::size_t frames) {
    RenderAudioBlock block;
    block.interleaved_stereo.assign(frames * 2U, 0.5F);
    return std::optional<RenderAudioBlock>{std::move(block)};
  };
  callbacks.cancelled = [] { return false; };

  TimelineRenderRuntime runtime(executor, callbacks, 1024 * 1024);
  const auto preview = runtime.render(TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 4);
  require(preview.success, "preview render failed");
  require(!preview.gpu_resident, "CPU fallback unexpectedly reported GPU residency");
  require(preview.used_cpu_fallback, "GPU-unavailable render did not report CPU fallback");
  require(preview.cpu_worker_threads >= 1, "CPU fallback did not report a worker");
  require(preview.decoded_video_layers == 2, "video layers were not decoded");
  require(preview.cache_misses == 2 && preview.cache_hits == 0, "initial cache accounting failed");
  require(preview.audio.interleaved_stereo.size() == 8, "audio mix was not produced");
  require(std::fabs(preview.video.rgba.front() - 0.55F) < 0.0001F, "composite value incorrect");

  const auto exported = runtime.render(TimelineExecutionMode::export_render, 500'000, 2, 1, 1, 1, 4);
  require(exported.success, "export render failed");
  require(exported.cache_hits == 2 && exported.cache_misses == 0, "cache reuse failed");
  require(preview.plan_identity == exported.plan_identity, "preview/export plan identity differs");
  require(preview.video.rgba == exported.video.rgba, "preview/export pixels differ");
  require(decode_calls == 2, "cached export decoded video again");

  const auto cpu_parity = runtime.verify_preview_export_parity(500'000, 2, 1, 1, 1);
  require(cpu_parity.verified && cpu_parity.equivalent,
          "exact CPU preview/export pixel parity was not verified");

  TimelineRenderRuntime strict_gpu_without_provider(
      executor, callbacks, 1024, 0, 0, RenderResidencyPolicy::require_gpu);
  const auto strict_missing = strict_gpu_without_provider.render(
      TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 0);
  require(!strict_missing.success,
          "strict GPU render silently fell back to CPU without a GPU provider");

  runtime.invalidate_clip("overlay");
  const auto invalidated = runtime.render(TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 4);
  require(invalidated.success, "render after invalidation failed");
  require(invalidated.cache_hits == 1 && invalidated.cache_misses == 1, "clip invalidation failed");

  int gpu_decode_calls = 0;
  std::vector<std::uint64_t> composited_identities;
  TimelineRenderCallbacks gpu_callbacks;
  gpu_callbacks.create_gpu_target = [](std::uint32_t width, std::uint32_t height,
                                       std::int64_t) {
    return std::optional<RenderVideoFrame>{make_gpu_frame(900, width, height, "gpu-target")};
  };
  gpu_callbacks.decode_video = [&](const VideoExecutionLayer& layer, bool) {
    ++gpu_decode_calls;
    const auto identity = layer.clip_id == "base" ? 101U : 102U;
    return std::optional<RenderVideoFrame>{
        make_gpu_frame(identity, 2, 1, ("gpu:" + layer.clip_id).c_str())};
  };
  gpu_callbacks.apply_effects = [](const VideoExecutionLayer&, RenderVideoFrame& frame) {
    return frame.gpu_resident() && frame.rgba.empty();
  };
  gpu_callbacks.composite = [&](const VideoExecutionLayer& layer,
                                const RenderVideoFrame& input,
                                RenderVideoFrame& output) {
    require(input.gpu_resident(), "GPU compositor received a CPU frame");
    require(output.gpu_resident(), "GPU compositor target is not resident");
    require(input.rgba.empty() && output.rgba.empty(), "GPU path allocated CPU pixels");
    require(layer.opacity >= 0.0 && layer.transition_weight >= 0.0,
            "GPU compositor did not receive layer weights");
    composited_identities.push_back(input.gpu->identity());
    output.provenance += "+" + input.provenance;
    return true;
  };
  gpu_callbacks.cancelled = [] { return false; };
  gpu_callbacks.compare_gpu_frames = [](const RenderVideoFrame& preview_frame,
                                        const RenderVideoFrame& export_frame,
                                        std::string& diagnostic) {
    const bool equal = preview_frame.gpu && export_frame.gpu &&
                       preview_frame.gpu->backend() == export_frame.gpu->backend() &&
                       preview_frame.width == export_frame.width &&
                       preview_frame.height == export_frame.height &&
                       preview_frame.provenance == export_frame.provenance;
    diagnostic = equal ? "test backend parity verified" : "test backend parity mismatch";
    return equal;
  };

  TimelineRenderRuntime gpu_runtime(executor, gpu_callbacks, 1, 4);
  const auto gpu_preview = gpu_runtime.render(
      TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 0);
  require(gpu_preview.success && gpu_preview.gpu_resident,
          "GPU preview did not remain resident");
  require(!gpu_preview.used_cpu_fallback, "GPU preview reported CPU fallback");
  require(gpu_preview.video.rgba.empty(), "GPU preview performed CPU readback");
  require(gpu_preview.cache_misses == 2 && gpu_preview.cache_hits == 0,
          "GPU initial cache accounting failed");

  const auto gpu_export = gpu_runtime.render(
      TimelineExecutionMode::export_render, 500'000, 2, 1, 1, 1, 0);
  require(gpu_export.success && gpu_export.gpu_resident,
          "GPU export did not remain resident");
  require(gpu_export.video.rgba.empty(), "GPU export performed CPU readback");
  require(gpu_export.cache_hits == 2 && gpu_export.cache_misses == 0,
          "GPU cache reuse failed");
  require(gpu_decode_calls == 2, "GPU export decoded cached frames again");
  require(composited_identities.size() == 4 &&
              composited_identities[0] == composited_identities[2] &&
              composited_identities[1] == composited_identities[3],
          "preview/export did not reuse identical GPU frame handles");

  const auto gpu_parity = gpu_runtime.verify_preview_export_parity(500'000, 2, 1, 1, 1);
  require(gpu_parity.verified && gpu_parity.equivalent,
          "GPU parity comparator was not required/executed");

  auto no_compare = gpu_callbacks;
  no_compare.compare_gpu_frames = {};
  TimelineRenderRuntime unverified_gpu_runtime(executor, no_compare, 1, 4);
  const auto unverified = unverified_gpu_runtime.verify_preview_export_parity(500'000, 2, 1, 1, 1);
  require(!unverified.verified,
          "GPU parity was falsely verified without backend pixel comparison");

  auto failing_target = gpu_callbacks;
  failing_target.create_gpu_target = [](std::uint32_t, std::uint32_t, std::int64_t)
      -> std::optional<RenderVideoFrame> { return std::nullopt; };
  TimelineRenderRuntime fail_closed_runtime(executor, failing_target, 1, 4);
  const auto fail_closed = fail_closed_runtime.render(
      TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 0);
  require(!fail_closed.success && !fail_closed.used_cpu_fallback,
          "selected GPU failure silently fell back to CPU");

  gpu_runtime.invalidate_clip("overlay");
  const auto gpu_invalidated = gpu_runtime.render(
      TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 0);
  require(gpu_invalidated.success, "GPU render after invalidation failed");
  require(gpu_invalidated.cache_hits == 1 && gpu_invalidated.cache_misses == 1,
          "GPU clip invalidation failed");

  static int foreign_context;
  auto mismatched_callbacks = gpu_callbacks;
  mismatched_callbacks.decode_video = [](const VideoExecutionLayer& layer, bool) {
    return std::optional<RenderVideoFrame>{make_gpu_frame(
        700, 2, 1, layer.clip_id.c_str(), &foreign_context)};
  };
  TimelineRenderRuntime mismatched_runtime(executor, mismatched_callbacks, 1, 4);
  const auto mismatched = mismatched_runtime.render(
      TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 0);
  require(!mismatched.success &&
              mismatched.diagnostic.find("device/context mismatch") != std::string::npos,
          "cross-device GPU frame was not rejected");

  int budget_decodes = 0;
  auto budget_callbacks = gpu_callbacks;
  budget_callbacks.decode_video = [&](const VideoExecutionLayer& layer, bool) {
    ++budget_decodes;
    return std::optional<RenderVideoFrame>{make_gpu_frame(
        layer.clip_id == "base" ? 801 : 802, 2, 1, layer.clip_id.c_str())};
  };
  TimelineRenderRuntime budget_runtime(executor, budget_callbacks, 1, 8, 32);
  require(budget_runtime.render(TimelineExecutionMode::preview, 500'000, 2, 1,
                                1, 1, 0).success,
          "byte-budget render failed");
  require(budget_runtime.gpu_cache_bytes() <= 32, "GPU cache exceeded byte budget");
  require(budget_runtime.render(TimelineExecutionMode::preview, 500'000, 2, 1,
                                1, 1, 0).success,
          "byte-budget repeated render failed");
  require(budget_decodes > 2, "byte-budget eviction did not force a cache miss");
  return 0;
}
