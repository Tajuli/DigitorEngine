#include "digitor/export_render_snapshot.hpp"
#include "digitor/flutter_production_host_adapter.hpp"
#include "digitor/production_hardware_encode.hpp"
#include "digitor/production_node_graph.hpp"
#include "digitor/timeline_completion.hpp"
#include "core/engine.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

/* The public C ABI intentionally keeps DigitorNodeGraph opaque. Native FFI
 * translation units share this exact wrapper layout and never expose it to
 * Flutter. */
struct DigitorNodeGraph {
  digitor::ProductionNodeGraph impl;
};

namespace digitor {
namespace flutter_timeline_service_detail {

struct ActiveVideoClip final {
  TimelineClipModel clip;
  std::string source_path;
};

ExportCodec export_codec(std::int32_t value) noexcept {
  switch (value) {
    case 0: return ExportCodec::h264;
    case 1: return ExportCodec::hevc;
    case 2: return ExportCodec::av1;
    case 3: return ExportCodec::prores;
    default: return ExportCodec::h264;
  }
}

std::optional<DigitorRendererBackend> renderer_backend(
    std::int32_t native_backend) noexcept {
  switch (native_backend) {
    case DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12:
      return DIGITOR_RENDERER_D3D12;
    case DIGITOR_NATIVE_TEXTURE_BACKEND_VULKAN:
      return DIGITOR_RENDERER_VULKAN;
    case DIGITOR_NATIVE_TEXTURE_BACKEND_METAL:
      return DIGITOR_RENDERER_METAL;
    case DIGITOR_NATIVE_TEXTURE_BACKEND_OPENGL_ES:
      return DIGITOR_RENDERER_OPENGL_ES;
    default:
      return std::nullopt;
  }
}

std::optional<ExportAlphaPolicy> alpha_policy(std::uint32_t value) noexcept {
  switch (value) {
    case 1: return ExportAlphaPolicy::discard;
    case 2: return ExportAlphaPolicy::straight;
    case 3: return ExportAlphaPolicy::premultiplied;
    default: return std::nullopt;
  }
}

bool supported_working_format(std::int32_t value) noexcept {
  return value == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
         value == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;
}

class Service final {
 public:
  explicit Service(const FlutterProductionHostAdapterInputs& inputs)
      : decoder_factory_(inputs.decoder_factory),
        preview_session_(inputs.preview_session),
        encoder_factory_(inputs.encoder_factory),
        texture_descriptor_builder_(inputs.texture_descriptor_builder),
        preview_target_binder_(inputs.preview_target_binder),
        preview_capabilities_(inputs.preview_capabilities),
        encoder_backend_(inputs.encoder_backend),
        default_video_bitrate_(inputs.video_bitrate),
        required_device_identity_(inputs.required_device_identity),
        required_context_identity_(inputs.required_context_identity),
        export_progress_ui_begin_(inputs.export_progress_ui_begin),
        export_progress_ui_update_(inputs.export_progress_ui_update),
        export_progress_ui_end_(inputs.export_progress_ui_end) {}

  [[nodiscard]] bool valid() const noexcept {
    return static_cast<bool>(decoder_factory_) && preview_session_ &&
           static_cast<bool>(texture_descriptor_builder_) &&
           static_cast<bool>(preview_target_binder_) &&
           preview_capabilities_.native_gpu_preview_available != 0 &&
           preview_capabilities_.cpu_fallback_only == 0;
  }

  DigitorResult configure(const char* serialized_project,
                          std::size_t serialized_size,
                          const DigitorFlutterTimelineSource* sources,
                          std::uint32_t source_count) noexcept {
    if (!serialized_project || serialized_size == 0 || !sources || source_count == 0)
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "serialized timeline and at least one source are required");
    try {
      auto decoded = TimelineCompletionEngine::deserialize(
          std::string(serialized_project, serialized_size));
      if (!decoded) {
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "production timeline serialization is invalid");
      }
      TimelineCompletionEngine validation(*decoded);
      if (!validation.validate()) {
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "production timeline failed native validation");
      }

      std::unordered_map<std::string, std::string> next_sources;
      for (std::uint32_t index = 0; index < source_count; ++index) {
        const auto& source = sources[index];
        if (source.struct_size < sizeof(DigitorFlutterTimelineSource) ||
            source.api_version != DIGITOR_FLUTTER_TIMELINE_SOURCE_VERSION ||
            !source.source_media_group_id || !source.source_media_group_id[0] ||
            !source.utf8_path || !source.utf8_path[0]) {
          return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                      "production timeline source registry contains an invalid entry");
        }
        next_sources.insert_or_assign(source.source_media_group_id,
                                      source.utf8_path);
      }

      bool has_video = false;
      for (const auto& track : decoded->timeline.tracks) {
        if (track.type != TimelineTrackType::video) continue;
        for (const auto& clip : track.clips) {
          if (clip.type != TimelineClipType::video) continue;
          has_video = true;
          if (clip.source_media_group_id.empty() ||
              !next_sources.contains(clip.source_media_group_id)) {
            return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                        "every production video clip must resolve to a registered source");
          }
        }
      }
      if (!has_video) {
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "production timeline contains no video clips");
      }

      std::scoped_lock lock(mutex_);
      if (export_running_ || operation_running_ || preview_generation_ != 0) {
        return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE,
                           "production timeline is busy; consume preview/export before replacing it");
      }
      project_ = std::move(*decoded);
      source_paths_ = std::move(next_sources);
      decoders_.clear();
      configured_ = true;
      cancelled_.store(false, std::memory_order_release);
      last_error_.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      return fail(DIGITOR_RESULT_OUT_OF_MEMORY,
                  "out of memory configuring production timeline");
    } catch (...) {
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "unexpected production timeline configuration failure");
    }
  }

  [[nodiscard]] bool configured() const noexcept {
    std::scoped_lock lock(mutex_);
    return configured_;
  }

  DigitorResult clear() noexcept {
    try {
      std::uint64_t generation = 0;
      {
        std::scoped_lock lock(mutex_);
        if (export_running_ || operation_running_) {
          return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE,
                             "cannot clear production timeline while rendering/exporting");
        }
        generation = preview_generation_;
        preview_generation_ = 0;
        configured_ = false;
        project_ = {};
        source_paths_.clear();
        decoders_.clear();
        cancelled_.store(false, std::memory_order_release);
        last_error_.clear();
      }
      if (generation != 0) preview_session_->consumed(generation);
      return DIGITOR_RESULT_OK;
    } catch (...) {
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "unexpected production timeline clear failure");
    }
  }

  DigitorResult query_preview(DigitorNativePreviewCapabilities* output) noexcept {
    if (!output) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
      auto value = preview_capabilities_;
      value.struct_size = sizeof(value);
      value.api_version = DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION;
      value.selected_mode = DIGITOR_PREVIEW_MODE_NATIVE_GPU_STRICT;
      value.native_gpu_preview_available = 1;
      value.cpu_fallback_only = 0;
      *output = value;
      return DIGITOR_RESULT_OK;
    } catch (...) {
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  DigitorResult set_preview_target(const DigitorFlutterPreviewTarget* target) noexcept {
    if (!target || target->struct_size < sizeof(DigitorFlutterPreviewTarget) ||
        target->api_version != DIGITOR_FLUTTER_PREVIEW_TARGET_VERSION ||
        target->native_target_handle == 0 || target->width == 0 ||
        target->height == 0 ||
        target->handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
        target->handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "invalid production timeline preview target");
    }
    try {
      std::string diagnostic;
      const auto result = preview_target_binder_(
          target->native_target_handle, target->width, target->height,
          target->handle_type, diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        return fail(result, diagnostic.empty()
                                ? "production timeline preview target binding failed"
                                : std::move(diagnostic));
      }
      std::scoped_lock lock(mutex_);
      last_error_.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      return fail(DIGITOR_RESULT_OUT_OF_MEMORY,
                  "out of memory binding production timeline preview target");
    } catch (...) {
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "unexpected production timeline preview target failure");
    }
  }

  DigitorResult preview(DigitorNodeGraph* graph,
                        std::uint64_t graph_revision,
                        std::uint64_t parameter_revision,
                        std::int64_t timeline_us,
                        std::uint32_t width,
                        std::uint32_t height,
                        DigitorNativeGpuTextureDescriptor* output) noexcept {
    if (output) *output = {};
    if (!graph || graph_revision == 0 || parameter_revision == 0 ||
        timeline_us < 0 || width == 0 || height == 0 || !output) {
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "invalid production timeline preview request");
    }
    try {
      {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
          return fail_locked(DIGITOR_RESULT_NOT_INITIALIZED,
                             "production timeline is not configured");
        }
        if (export_running_ || operation_running_ || preview_generation_ != 0) {
          return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE,
                             "production timeline already has an active operation/frame");
        }
        operation_running_ = true;
      }

      ProcessedGpuFramePtr frame;
      std::string diagnostic;
      auto result = process_frame(graph, timeline_us, width, height, frame, diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        finish_operation();
        return fail(result, std::move(diagnostic));
      }

      std::uint64_t generation = 0;
      {
        std::scoped_lock lock(mutex_);
        generation = ++generation_clock_;
        if (generation == 0) generation = ++generation_clock_;
      }
      const auto submitted = preview_session_->submit(frame, generation);
      if (!submitted) {
        finish_operation();
        return fail(submitted.result,
                    submitted.diagnostic.empty()
                        ? "native timeline GPU preview presentation failed"
                        : submitted.diagnostic);
      }

      DigitorNativeGpuTextureDescriptor descriptor{};
      descriptor.struct_size = sizeof(descriptor);
      descriptor.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
      result = texture_descriptor_builder_(frame, generation, descriptor, diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        preview_session_->consumed(generation);
        finish_operation();
        return fail(result, diagnostic.empty()
                                ? "failed to build native timeline preview descriptor"
                                : std::move(diagnostic));
      }
      if (descriptor.native_handle == 0 || descriptor.width != width ||
          descriptor.height != height || descriptor.generation != generation ||
          descriptor.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_NONE ||
          descriptor.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_CPU_RGBA8 ||
          descriptor.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
          descriptor.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
        preview_session_->consumed(generation);
        finish_operation();
        return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                    "native timeline preview descriptor is not GPU production-ready");
      }
      if (required_device_identity_ != 0 &&
          descriptor.device_identity != required_device_identity_) {
        preview_session_->consumed(generation);
        finish_operation();
        return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                    "timeline preview device identity differs from registered production host");
      }
      if (required_context_identity_ != 0 &&
          descriptor.context_identity != required_context_identity_) {
        preview_session_->consumed(generation);
        finish_operation();
        return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                    "timeline preview context identity differs from registered production host");
      }

      descriptor.timestamp_us = timeline_us;
      descriptor.generation = generation;
      {
        std::scoped_lock lock(mutex_);
        preview_generation_ = generation;
        operation_running_ = false;
        last_error_.clear();
      }
      *output = descriptor;
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      finish_operation();
      return fail(DIGITOR_RESULT_OUT_OF_MEMORY,
                  "out of memory rendering production timeline preview");
    } catch (...) {
      finish_operation();
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "unexpected production timeline preview failure");
    }
  }

  DigitorResult preview_consumed(std::uint64_t generation) noexcept {
    if (generation == 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
      {
        std::scoped_lock lock(mutex_);
        if (preview_generation_ == 0) return DIGITOR_RESULT_OK;
        if (preview_generation_ != generation) {
          return fail_locked(DIGITOR_RESULT_INVALID_ARGUMENT,
                             "production timeline preview generation mismatch");
        }
        preview_generation_ = 0;
      }
      preview_session_->consumed(generation);
      return DIGITOR_RESULT_OK;
    } catch (...) {
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "unexpected production timeline preview release failure");
    }
  }

  DigitorResult export_v2(DigitorNodeGraph* graph,
                          std::uint64_t graph_revision,
                          std::uint64_t parameter_revision,
                          const DigitorFlutterExportRequestV2* request,
                          DigitorExportProgressCallback progress,
                          void* progress_user_data) noexcept {
    if (!valid_export_request(graph, graph_revision, parameter_revision, request)) {
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "invalid frozen production timeline export request");
    }
    if (!encoder_factory_) {
      return fail(DIGITOR_RESULT_NOT_INITIALIZED,
                  "native timeline export encoder factory is unavailable");
    }
    const auto renderer = renderer_backend(preview_capabilities_.backend);
    const auto alpha = alpha_policy(request->alpha_policy);
    if (!renderer || !alpha) {
      return fail(DIGITOR_RESULT_UNSUPPORTED,
                  "production timeline export backend or alpha policy is unsupported");
    }

    bool progress_ui_started = false;
    try {
      {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
          return fail_locked(DIGITOR_RESULT_NOT_INITIALIZED,
                             "production timeline is not configured");
        }
        if (preview_generation_ != 0 || operation_running_ || export_running_) {
          return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE,
                             "production timeline is busy; consume preview before export");
        }
        if (request->timeline_revision != project_.revision) {
          return fail_locked(DIGITOR_RESULT_INVALID_ARGUMENT,
                             "frozen export timeline revision differs from configured native timeline");
        }
        export_running_ = true;
        cancelled_.store(false, std::memory_order_release);
      }

      if (graph_revision != request->node_graph_revision ||
          parameter_revision != request->color_pipeline_revision) {
        clear_export_state();
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "frozen export graph/color revision differs from bound node graph");
      }
      if (graph->impl.recipe_identity() != request->utf8_graph_recipe_identity) {
        clear_export_state();
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    "frozen export graph recipe identity differs from bound node graph");
      }

      ExportRenderSnapshotData data{};
      data.snapshot_identity = request->snapshot_identity;
      data.timeline_revision = request->timeline_revision;
      data.render_revision = request->render_revision;
      data.node_graph_revision = request->node_graph_revision;
      data.color_pipeline_revision = request->color_pipeline_revision;
      data.audio_revision = request->audio_revision;
      data.width = request->width;
      data.height = request->height;
      data.working_format = static_cast<DigitorPixelFormat>(request->working_format);
      data.alpha_policy = *alpha;
      data.fps_num = request->fps_num;
      data.fps_den = request->fps_den;
      data.duration_us = request->duration_us;
      data.variable_frame_rate = false;
      data.hdr = request->hdr != 0;
      data.color_metadata = request->utf8_color_metadata;
      // Do not mux the last imported source's audio into a multi-source export.
      // Project audio mixing will be bound independently; wrong audio is never
      // accepted as a fallback for correct video.
      data.source_media_path.clear();
      data.source_start_us = 0;
      data.output_path = request->utf8_output_path;
      data.profile.codec = export_codec(request->codec);
      data.profile.width = static_cast<std::int32_t>(request->width);
      data.profile.height = static_cast<std::int32_t>(request->height);
      data.profile.fps_num = request->fps_num;
      data.profile.fps_den = request->fps_den;
      data.profile.video_bitrate = request->video_bitrate > 0
                                       ? request->video_bitrate
                                       : default_video_bitrate_;
      data.profile.ten_bit = request->ten_bit != 0;
      data.profile.prefer_hardware = true;
      data.profile.allow_software_fallback = false;
      data.policy = ExportExecutionPolicy::hardware_required;
      data.renderer_backend = *renderer;
      data.encoder_backend = encoder_backend_;

      auto snapshot = std::make_shared<const ExportRenderSnapshot>(std::move(data));
      const auto snapshot_validation = validate_export_snapshot(*snapshot);
      if (!snapshot_validation) {
        clear_export_state();
        return fail(snapshot_validation.result, snapshot_validation.diagnostic);
      }
      auto encoder_result = encoder_factory_(snapshot);
      if (!encoder_result) {
        clear_export_state();
        return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                    encoder_result.diagnostic.empty()
                        ? "failed to create native timeline hardware encoder"
                        : encoder_result.diagnostic);
      }

      HardwareEncodeConfig config{};
      config.profile = snapshot->data().profile;
      config.backend = snapshot->encoder_backend();
      config.output_path = snapshot->data().output_path;
      config.duration_us = snapshot->data().duration_us;
      config.require_hardware = true;
      config.require_zero_copy = true;
      config.require_monotonic_timestamps = true;
      config.require_atomic_finalize = true;

      ProductionHardwareEncodeSession encoder(
          config, std::move(encoder_result.callbacks));
      {
        std::scoped_lock lock(mutex_);
        active_encoder_ = &encoder;
      }
      if (export_progress_ui_begin_) {
        try { export_progress_ui_begin_(); } catch (...) {}
      }
      progress_ui_started = true;

      std::string diagnostic;
      auto result = encoder.start(&diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        finish_export_ui(result, progress_ui_started);
        clear_export_state();
        return fail(result, diagnostic.empty()
                                ? "failed to start native timeline hardware encoder"
                                : std::move(diagnostic));
      }

      const std::uint64_t total = static_cast<std::uint64_t>(
          request->last_frame - request->first_frame + 1);
      if (progress) progress(0.0, 0, static_cast<std::int64_t>(total), progress_user_data);
      std::uint64_t completed = 0;
      std::int64_t output_pts_us = 0;
      std::int64_t cadence_remainder = 0;
      const auto cadence_numerator =
          1'000'000LL * static_cast<std::int64_t>(request->fps_den);
      const auto cadence_denominator = static_cast<std::int64_t>(request->fps_num);

      for (std::int64_t frame_index = request->first_frame;
           frame_index <= request->last_frame; ++frame_index) {
        if (cancelled_.load(std::memory_order_acquire)) {
          encoder.cancel();
          finish_export_ui(DIGITOR_RESULT_RESOURCE_IN_USE, progress_ui_started);
          clear_export_state();
          return fail(DIGITOR_RESULT_RESOURCE_IN_USE,
                      "production timeline export cancelled");
        }

        const auto timeline_us = static_cast<std::int64_t>(
            (static_cast<long double>(frame_index) * 1'000'000.0L *
             static_cast<long double>(request->fps_den)) /
            static_cast<long double>(request->fps_num));
        ProcessedGpuFramePtr rendered;
        diagnostic.clear();
        result = process_frame(graph, timeline_us, request->width,
                               request->height, rendered, diagnostic);
        if (result != DIGITOR_RESULT_OK) {
          encoder.cancel();
          finish_export_ui(result, progress_ui_started);
          clear_export_state();
          return fail(result, diagnostic.empty()
                                  ? "native timeline frame render failed during export"
                                  : std::move(diagnostic));
        }
        const auto frame_validation = validate_frame_against_snapshot(
            *snapshot, *rendered);
        if (!frame_validation) {
          encoder.cancel();
          finish_export_ui(frame_validation.result, progress_ui_started);
          clear_export_state();
          return fail(frame_validation.result, frame_validation.diagnostic);
        }

        std::int64_t output_duration_us = cadence_numerator / cadence_denominator;
        cadence_remainder += cadence_numerator % cadence_denominator;
        if (cadence_remainder >= cadence_denominator) {
          output_duration_us += cadence_remainder / cadence_denominator;
          cadence_remainder %= cadence_denominator;
        }
        if (output_duration_us <= 0) output_duration_us = 1;

        HardwareEncodeFrame encode_frame{};
        encode_frame.frame = std::move(rendered);
        encode_frame.pts_us = output_pts_us;
        encode_frame.duration_us = output_duration_us;
        result = encoder.submit(std::move(encode_frame), &diagnostic);
        if (result != DIGITOR_RESULT_OK) {
          encoder.cancel();
          finish_export_ui(result, progress_ui_started);
          clear_export_state();
          return fail(result, diagnostic.empty()
                                  ? "native timeline GPU frame submission failed"
                                  : std::move(diagnostic));
        }
        output_pts_us += output_duration_us;
        ++completed;
        const auto fraction = static_cast<double>(completed) /
                              static_cast<double>(total);
        if (export_progress_ui_update_) {
          try {
            export_progress_ui_update_(fraction,
                                       static_cast<std::int64_t>(completed),
                                       static_cast<std::int64_t>(total));
          } catch (...) {}
        }
        if (progress) {
          progress(fraction, static_cast<std::int64_t>(completed),
                   static_cast<std::int64_t>(total), progress_user_data);
        }
      }

      result = encoder.finish(&diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        finish_export_ui(result, progress_ui_started);
        clear_export_state();
        return fail(result, diagnostic.empty()
                                ? "failed to finalize native timeline export"
                                : std::move(diagnostic));
      }
      if (!encoder_result.zero_copy_qualified ||
          !encoder_result.zero_copy_qualified()) {
        finish_export_ui(DIGITOR_RESULT_INTERNAL_ERROR, progress_ui_started);
        clear_export_state();
        return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                    "native timeline export completed without zero-copy qualification");
      }
      finish_export_ui(DIGITOR_RESULT_OK, progress_ui_started);
      clear_export_state();
      if (progress) {
        progress(1.0, static_cast<std::int64_t>(total),
                 static_cast<std::int64_t>(total), progress_user_data);
      }
      {
        std::scoped_lock lock(mutex_);
        last_error_.clear();
      }
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      finish_export_ui(DIGITOR_RESULT_OUT_OF_MEMORY, progress_ui_started);
      clear_export_state();
      return fail(DIGITOR_RESULT_OUT_OF_MEMORY,
                  "out of memory exporting native production timeline");
    } catch (...) {
      finish_export_ui(DIGITOR_RESULT_INTERNAL_ERROR, progress_ui_started);
      clear_export_state();
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                  "unexpected native production timeline export failure");
    }
  }

  DigitorResult cancel() noexcept {
    cancelled_.store(true, std::memory_order_release);
    ProductionHardwareEncodeSession* encoder = nullptr;
    {
      std::scoped_lock lock(mutex_);
      encoder = active_encoder_;
    }
    if (encoder) encoder->cancel();
    return DIGITOR_RESULT_OK;
  }

  std::string last_error() const {
    std::scoped_lock lock(mutex_);
    return last_error_;
  }

 private:
  bool valid_export_request(DigitorNodeGraph* graph,
                            std::uint64_t graph_revision,
                            std::uint64_t parameter_revision,
                            const DigitorFlutterExportRequestV2* request) const noexcept {
    return graph && graph_revision != 0 && parameter_revision != 0 && request &&
           request->struct_size >= sizeof(DigitorFlutterExportRequestV2) &&
           request->api_version == DIGITOR_FLUTTER_EXPORT_REQUEST_V2_VERSION &&
           request->utf8_output_path && request->utf8_output_path[0] &&
           request->utf8_color_metadata && request->utf8_color_metadata[0] &&
           request->utf8_graph_recipe_identity && request->utf8_graph_recipe_identity[0] &&
           request->first_frame >= 0 && request->last_frame >= request->first_frame &&
           request->width != 0 && request->height != 0 &&
           request->snapshot_identity != 0 && request->timeline_revision != 0 &&
           request->render_revision != 0 && request->node_graph_revision != 0 &&
           request->color_pipeline_revision != 0 && request->audio_revision != 0 &&
           request->fps_num > 0 && request->fps_den > 0 &&
           request->duration_us > 0 && request->video_bitrate > 0 &&
           request->variable_frame_rate == 0 &&
           supported_working_format(request->working_format);
  }

  DigitorResult process_frame(DigitorNodeGraph* graph,
                              std::int64_t timeline_us,
                              std::uint32_t width,
                              std::uint32_t height,
                              ProcessedGpuFramePtr& output,
                              std::string& diagnostic) noexcept {
    output.reset();
    if (cancelled_.load(std::memory_order_acquire)) {
      diagnostic = "production timeline render is cancelled";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    try {
      ActiveVideoClip active;
      ProductionHardwareDecodeSession* decoder = nullptr;
      {
        std::scoped_lock lock(mutex_);
        if (!configured_) {
          diagnostic = "production timeline is not configured";
          return DIGITOR_RESULT_NOT_INITIALIZED;
        }
        auto resolved = resolve_active_clip_locked(timeline_us, diagnostic);
        if (!resolved) return DIGITOR_RESULT_UNSUPPORTED;
        active = std::move(*resolved);

        auto found = decoders_.find(active.source_path);
        if (found == decoders_.end()) {
          std::string open_diagnostic;
          auto created = decoder_factory_(active.source_path, open_diagnostic);
          if (!created) {
            diagnostic = open_diagnostic.empty()
                             ? "native production decoder could not open timeline source"
                             : std::move(open_diagnostic);
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }
          found = decoders_.emplace(active.source_path, std::move(created)).first;
        }
        decoder = found->second.get();
      }

      const auto source_time_us = active.clip.source_start_us +
                                  (timeline_us - active.clip.start_us);
      if (source_time_us < 0 ||
          (active.clip.source_duration_us &&
           source_time_us >= *active.clip.source_duration_us)) {
        diagnostic = "timeline source time is outside the clip source range";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }

      ProductionDecodedFrame decoded;
      std::string decode_diagnostic;
      auto result = decoder->decode_at_timestamp(source_time_us, decoded,
                                                 &decode_diagnostic);
      if (result != DIGITOR_RESULT_OK || !decoded.gpu_frame ||
          !decoded.gpu_frame->ready()) {
        diagnostic = decode_diagnostic.empty()
                         ? "strict GPU timeline source decode failed"
                         : std::move(decode_diagnostic);
        return result == DIGITOR_RESULT_OK ? DIGITOR_RESULT_BACKEND_UNAVAILABLE
                                           : result;
      }
      if (!Engine::instance().production_frame_bound_to_current_backend(
              decoded.gpu_frame)) {
        diagnostic =
            "decoded timeline frame is not bound to the selected production GPU backend/context";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }

      auto rendered = Engine::instance().execute_native_node_graph(
          graph->impl, decoded.gpu_frame, decoded.pts);
      if (rendered.status != NativeNodeGraphStatus::ok ||
          rendered.backend_result != DIGITOR_RESULT_OK || !rendered.frame ||
          !rendered.frame->ready()) {
        diagnostic = rendered.message.empty()
                         ? "native node/color graph failed for timeline clip"
                         : std::move(rendered.message);
        return rendered.backend_result == DIGITOR_RESULT_OK
                   ? DIGITOR_RESULT_INTERNAL_ERROR
                   : rendered.backend_result;
      }
      if (!Engine::instance().production_frame_bound_to_current_backend(
              rendered.frame)) {
        diagnostic =
            "rendered timeline frame left the selected production GPU backend/context";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      const auto& metadata = rendered.frame->metadata();
      if (metadata.width != width || metadata.height != height) {
        diagnostic =
            "timeline source dimensions differ from project output; native scale/composite path is required";
        return DIGITOR_RESULT_UNSUPPORTED;
      }
      output = std::move(rendered.frame);
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory rendering native production timeline frame";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      diagnostic = "unexpected native production timeline frame failure";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  std::optional<ActiveVideoClip> resolve_active_clip_locked(
      std::int64_t timeline_us, std::string& diagnostic) const {
    const TimelineClipModel* active = nullptr;
    for (const auto& track : project_.timeline.tracks) {
      if (track.type != TimelineTrackType::video || track.hidden || track.muted)
        continue;
      for (const auto& clip : track.clips) {
        if (clip.type != TimelineClipType::video || !clip.visible || clip.muted)
          continue;
        if (timeline_us < clip.start_us ||
            timeline_us >= clip.start_us + clip.duration_us) continue;
        if (active != nullptr) {
          diagnostic =
              "overlapping video layers require native timeline GPU compositing; refusing to render only one layer";
          return std::nullopt;
        }
        active = &clip;
      }
    }
    if (!active) {
      diagnostic =
          "timeline position contains no active video clip; GPU gap-frame generation is not configured";
      return std::nullopt;
    }
    const auto source = source_paths_.find(active->source_media_group_id);
    if (source == source_paths_.end()) {
      diagnostic = "active timeline clip source is not registered";
      return std::nullopt;
    }
    return ActiveVideoClip{*active, source->second};
  }

  DigitorResult fail(DigitorResult result, std::string diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    return fail_locked(result, std::move(diagnostic));
  }

  DigitorResult fail_locked(DigitorResult result,
                            std::string diagnostic) noexcept {
    try { last_error_ = std::move(diagnostic); } catch (...) { last_error_.clear(); }
    return result;
  }

  void finish_operation() noexcept {
    std::scoped_lock lock(mutex_);
    operation_running_ = false;
  }

  void clear_export_state() noexcept {
    std::scoped_lock lock(mutex_);
    active_encoder_ = nullptr;
    export_running_ = false;
  }

  void finish_export_ui(DigitorResult result, bool started) noexcept {
    if (!started || !export_progress_ui_end_) return;
    try { export_progress_ui_end_(result); } catch (...) {}
  }

  ProductionDecoderFactory decoder_factory_;
  std::shared_ptr<NativePreviewPresentationSession> preview_session_;
  ProductionEncoderFactory encoder_factory_;
  ProductionTextureDescriptorBuilder texture_descriptor_builder_;
  ProductionPreviewTargetBinder preview_target_binder_;
  DigitorNativePreviewCapabilities preview_capabilities_{};
  EncoderBackend encoder_backend_{EncoderBackend::software};
  std::int64_t default_video_bitrate_{12'000'000};
  std::uint64_t required_device_identity_{};
  std::uint64_t required_context_identity_{};
  ProductionExportProgressUiBegin export_progress_ui_begin_;
  ProductionExportProgressUiUpdate export_progress_ui_update_;
  ProductionExportProgressUiEnd export_progress_ui_end_;

  mutable std::mutex mutex_;
  TimelineCompletionProject project_{};
  std::unordered_map<std::string, std::string> source_paths_;
  std::unordered_map<std::string,
                     std::unique_ptr<ProductionHardwareDecodeSession>> decoders_;
  ProductionHardwareEncodeSession* active_encoder_{};
  std::uint64_t generation_clock_{};
  std::uint64_t preview_generation_{};
  bool configured_{};
  bool operation_running_{};
  bool export_running_{};
  std::atomic_bool cancelled_{false};
  std::string last_error_;
};

std::mutex g_service_mutex;
std::shared_ptr<Service> g_service;

std::shared_ptr<Service> active_service() noexcept {
  std::scoped_lock lock(g_service_mutex);
  return g_service;
}

DigitorResult copy_error(const std::string& value, char* buffer,
                         std::uint32_t* inout_size) noexcept {
  if (!inout_size) return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (value.size() >= static_cast<std::size_t>(UINT32_MAX))
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  const auto required = static_cast<std::uint32_t>(value.size() + 1U);
  if (!buffer) {
    *inout_size = required;
    return DIGITOR_RESULT_OK;
  }
  if (*inout_size < required) {
    *inout_size = required;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  std::memcpy(buffer, value.c_str(), required);
  *inout_size = required;
  return DIGITOR_RESULT_OK;
}

}  // namespace flutter_timeline_service_detail

DigitorResult install_flutter_production_timeline_service(
    const FlutterProductionHostAdapterInputs& inputs) noexcept {
  try {
    auto service = std::make_shared<flutter_timeline_service_detail::Service>(inputs);
    if (!service->valid()) return DIGITOR_RESULT_NOT_INITIALIZED;
    std::scoped_lock lock(flutter_timeline_service_detail::g_service_mutex);
    if (flutter_timeline_service_detail::g_service)
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    flutter_timeline_service_detail::g_service = std::move(service);
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

void uninstall_flutter_production_timeline_service() noexcept {
  std::shared_ptr<flutter_timeline_service_detail::Service> service;
  {
    std::scoped_lock lock(flutter_timeline_service_detail::g_service_mutex);
    service = std::move(flutter_timeline_service_detail::g_service);
  }
  if (service) {
    service->cancel();
    (void)service->clear();
  }
}

}  // namespace digitor

extern "C" {

DigitorResult digitor_flutter_production_timeline_configure(
    const char* serialized_project,
    size_t serialized_size,
    const DigitorFlutterTimelineSource* sources,
    uint32_t source_count) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->configure(serialized_project, serialized_size, sources,
                            source_count);
}

int32_t digitor_flutter_production_timeline_configured(void) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  return service && service->configured() ? 1 : 0;
}

DigitorResult digitor_flutter_production_timeline_clear(void) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->clear();
}

DigitorResult digitor_flutter_production_timeline_query_preview(
    DigitorNativePreviewCapabilities* out_capabilities) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->query_preview(out_capabilities);
}

DigitorResult digitor_flutter_production_timeline_set_preview_target(
    const DigitorFlutterPreviewTarget* target) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->set_preview_target(target);
}

DigitorResult digitor_flutter_production_timeline_preview(
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision,
    int64_t timeline_timestamp_us,
    uint32_t width,
    uint32_t height,
    DigitorNativeGpuTextureDescriptor* out_texture) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->preview(graph, graph_revision, parameter_revision,
                          timeline_timestamp_us, width, height, out_texture);
}

DigitorResult digitor_flutter_production_timeline_preview_consumed(
    uint64_t generation) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->preview_consumed(generation);
}

DigitorResult digitor_flutter_production_timeline_export_v2(
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision,
    const DigitorFlutterExportRequestV2* request,
    DigitorExportProgressCallback progress,
    void* progress_user_data) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->export_v2(graph, graph_revision, parameter_revision, request,
                            progress, progress_user_data);
}

DigitorResult digitor_flutter_production_timeline_cancel(void) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return service->cancel();
}

DigitorResult digitor_flutter_production_timeline_get_last_error(
    char* buffer,
    uint32_t* inout_size) {
  auto service = digitor::flutter_timeline_service_detail::active_service();
  if (!service) return DIGITOR_RESULT_NOT_INITIALIZED;
  return digitor::flutter_timeline_service_detail::copy_error(
      service->last_error(), buffer, inout_size);
}

}  // extern "C"
