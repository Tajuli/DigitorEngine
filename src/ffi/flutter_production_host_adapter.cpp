#include "digitor/flutter_production_host_adapter.hpp"

#include "digitor/production_node_graph.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

struct DigitorNodeGraph { digitor::ProductionNodeGraph impl; };

namespace digitor {
namespace {
void write_diagnostic(char* output, std::uint32_t capacity,
                      const std::string& value) noexcept {
  if (!output || capacity == 0) return;
  const auto count = std::min<std::size_t>(capacity - 1, value.size());
  if (count) std::memcpy(output, value.data(), count);
  output[count] = '\0';
}

ExportCodec export_codec(std::int32_t value) noexcept {
  switch (value) {
    case 0: return ExportCodec::h264;
    case 1: return ExportCodec::hevc;
    case 2: return ExportCodec::av1;
    case 3: return ExportCodec::prores;
    default: return ExportCodec::h264;
  }
}

bool callbacks_complete(const HardwareEncoderCallbacks& value) noexcept {
  return value.open && value.submit_gpu_frame && value.drain &&
         value.finalize_atomic && value.cancel;
}

bool native_preview_capabilities_valid(
    const DigitorNativePreviewCapabilities& value) noexcept {
  return value.backend != DIGITOR_NATIVE_TEXTURE_BACKEND_NONE &&
         value.backend != DIGITOR_NATIVE_TEXTURE_BACKEND_CPU_RGBA8 &&
         value.handle_type != DIGITOR_NATIVE_TEXTURE_HANDLE_NONE &&
         value.handle_type != DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER;
}
}  // namespace

struct FlutterProductionHostAdapter::Impl {
  explicit Impl(FlutterProductionHostAdapterInputs value)
      : inputs(std::move(value)) {}

  FlutterProductionHostAdapterInputs inputs;
  std::mutex mutex;
  std::string media_path;
  std::unique_ptr<ProductionHardwareDecodeSession> pending_decoder;
  std::unique_ptr<ProductionMediaGraphRuntime> runtime;
  DigitorNodeGraph* bound_graph{};
  std::string bound_identity;
  std::uint64_t graph_revision{};
  std::uint64_t parameter_revision{};
  std::uint64_t generation{};
  bool opened{};

  bool valid() const noexcept {
    return inputs.decoder_factory && inputs.frame_resolver &&
           inputs.preview_session && inputs.texture_descriptor_builder &&
           inputs.preview_target_binder &&
           callbacks_complete(inputs.encoder_callbacks) &&
           inputs.encoder_backend != EncoderBackend::software &&
           inputs.fps_num > 0 && inputs.fps_den > 0 &&
           native_preview_capabilities_valid(inputs.preview_capabilities);
  }

  DigitorResult make_decoder(
      std::unique_ptr<ProductionHardwareDecodeSession>& output,
      std::string& diagnostic) {
    output = inputs.decoder_factory(media_path, diagnostic);
    if (output) return DIGITOR_RESULT_OK;
    if (diagnostic.empty())
      diagnostic = "production decoder factory returned no decoder";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  DigitorResult ensure_runtime(DigitorNodeGraph* graph,
                               std::uint64_t graph_rev,
                               std::uint64_t parameter_rev,
                               std::string& diagnostic) {
    if (!graph || !opened) return DIGITOR_RESULT_NOT_INITIALIZED;
    std::string identity;
    try { identity = graph->impl.recipe_identity(); }
    catch (...) {
      diagnostic = "failed to read production node graph identity";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    if (runtime && bound_graph == graph && graph_revision == graph_rev &&
        parameter_revision == parameter_rev && bound_identity == identity)
      return DIGITOR_RESULT_OK;

    runtime.reset();
    std::unique_ptr<ProductionHardwareDecodeSession> decoder;
    if (pending_decoder) decoder = std::move(pending_decoder);
    else {
      const auto result = make_decoder(decoder, diagnostic);
      if (result != DIGITOR_RESULT_OK) return result;
    }

    auto presenter = [this](const ProcessedGpuFramePtr& frame,
                            std::string& diagnostic) -> DigitorResult {
      const auto current_generation = generation;
      if (!current_generation) {
        diagnostic = "preview generation was not reserved";
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }
      const auto submitted =
          inputs.preview_session->submit(frame, current_generation);
      if (submitted) return DIGITOR_RESULT_OK;
      diagnostic = submitted.diagnostic.empty()
          ? "native Flutter texture presentation failed"
          : submitted.diagnostic;
      return submitted.result;
    };

    try {
      runtime = std::make_unique<ProductionMediaGraphRuntime>(
          std::move(decoder), graph->impl, std::move(presenter),
          inputs.encoder_callbacks);
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory creating production media graph runtime";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      diagnostic = "failed to create production media graph runtime";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    bound_graph = graph;
    bound_identity = std::move(identity);
    graph_revision = graph_rev;
    parameter_revision = parameter_rev;
    return DIGITOR_RESULT_OK;
  }

  static Impl* self(void* value) noexcept { return static_cast<Impl*>(value); }

  static DigitorResult open_media(void* user_data, const char* path,
                                  char* diagnostic,
                                  std::uint32_t diagnostic_capacity) {
    auto* p = self(user_data);
    if (!p || !path || !path[0]) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::string message;
    try {
      std::lock_guard lock(p->mutex);
      if (p->opened) return DIGITOR_RESULT_RESOURCE_IN_USE;
      p->media_path = path;
      const auto result = p->make_decoder(p->pending_decoder, message);
      if (result != DIGITOR_RESULT_OK) {
        p->media_path.clear();
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return result;
      }
      p->opened = true;
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      write_diagnostic(diagnostic, diagnostic_capacity,
                       "out of memory opening production media");
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      write_diagnostic(diagnostic, diagnostic_capacity,
                       "unexpected production media open failure");
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  static DigitorResult render_frame(
      void* user_data, DigitorFlutterProductionRenderMode mode,
      DigitorNodeGraph* graph, std::uint64_t graph_rev,
      std::uint64_t parameter_rev, std::int64_t timestamp_us,
      std::uint32_t width, std::uint32_t height,
      DigitorNativeGpuTextureDescriptor* out_texture,
      char* diagnostic, std::uint32_t diagnostic_capacity) {
    auto* p = self(user_data);
    if (!p || mode != DIGITOR_FLUTTER_RENDER_PREVIEW || !graph ||
        !out_texture || !width || !height || timestamp_us < 0)
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    std::string message;
    ProductionMediaGraphRuntime* runtime = nullptr;
    FrameNumber frame_number{};
    std::uint64_t current_generation{};
    try {
      {
        std::lock_guard lock(p->mutex);
        const auto result =
            p->ensure_runtime(graph, graph_rev, parameter_rev, message);
        if (result != DIGITOR_RESULT_OK) {
          write_diagnostic(diagnostic, diagnostic_capacity, message);
          return result;
        }
        runtime = p->runtime.get();
        current_generation = ++p->generation;
        frame_number = p->inputs.frame_resolver(timestamp_us);
      }

      ProcessedGpuFramePtr frame;
      const auto render_result = runtime->preview(
          frame_number, &frame, &message);
      if (render_result != DIGITOR_RESULT_OK) {
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return render_result;
      }
      if (!frame || frame->metadata().width != width ||
          frame->metadata().height != height) {
        p->inputs.preview_session->consumed(current_generation);
        write_diagnostic(diagnostic, diagnostic_capacity,
            "production preview dimensions do not match the rendered GPU frame");
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }

      DigitorNativeGpuTextureDescriptor descriptor{};
      descriptor.struct_size = sizeof(descriptor);
      descriptor.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
      const auto descriptor_result = p->inputs.texture_descriptor_builder(
          frame, current_generation, descriptor, message);
      if (descriptor_result != DIGITOR_RESULT_OK) {
        p->inputs.preview_session->consumed(current_generation);
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return descriptor_result;
      }
      descriptor.struct_size = sizeof(descriptor);
      descriptor.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
      descriptor.timestamp_us = timestamp_us;
      descriptor.generation = current_generation;
      *out_texture = descriptor;
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      write_diagnostic(diagnostic, diagnostic_capacity,
                       "out of memory rendering production preview");
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      write_diagnostic(diagnostic, diagnostic_capacity,
                       "unexpected production preview adapter failure");
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  static DigitorResult export_media(
      void* user_data, DigitorNodeGraph* graph, std::uint64_t graph_rev,
      std::uint64_t parameter_rev, const DigitorFlutterExportRequest* request,
      DigitorExportProgressCallback progress, void* progress_user_data,
      char* diagnostic, std::uint32_t diagnostic_capacity) {
    auto* p = self(user_data);
    if (!p || !graph || !request || !request->utf8_output_path ||
        request->last_frame < request->first_frame || request->first_frame < 0 ||
        !request->width || !request->height || request->codec < 0 ||
        request->codec > 3) return DIGITOR_RESULT_INVALID_ARGUMENT;

    std::string message;
    ProductionMediaGraphRuntime* runtime = nullptr;
    HardwareEncodeConfig config{};
    try {
      {
        std::lock_guard lock(p->mutex);
        const auto result =
            p->ensure_runtime(graph, graph_rev, parameter_rev, message);
        if (result != DIGITOR_RESULT_OK) {
          write_diagnostic(diagnostic, diagnostic_capacity, message);
          return result;
        }
        runtime = p->runtime.get();
        config.profile.codec = export_codec(request->codec);
        config.profile.width = static_cast<std::int32_t>(request->width);
        config.profile.height = static_cast<std::int32_t>(request->height);
        config.profile.fps_num = p->inputs.fps_num;
        config.profile.fps_den = p->inputs.fps_den;
        config.profile.video_bitrate = p->inputs.video_bitrate;
        config.profile.prefer_hardware = true;
        config.profile.allow_software_fallback = false;
        config.backend = p->inputs.encoder_backend;
        config.output_path = request->utf8_output_path;
      }

      std::vector<FrameNumber> frames;
      frames.reserve(static_cast<std::size_t>(
          request->last_frame - request->first_frame + 1));
      for (auto frame = request->first_frame; frame <= request->last_frame; ++frame)
        frames.push_back(frame);
      config.duration_us = static_cast<std::int64_t>(frames.size()) *
          1'000'000LL * config.profile.fps_den / config.profile.fps_num;
      config.require_hardware = true;
      config.require_zero_copy = true;
      config.require_monotonic_timestamps = true;
      config.require_atomic_finalize = true;

      auto progress_bridge = [progress, progress_user_data](
          std::uint64_t completed, std::uint64_t total) {
        if (!progress) return;
        const auto fraction = total == 0 ? 0.0
            : static_cast<double>(completed) / static_cast<double>(total);
        progress(fraction, static_cast<std::int64_t>(completed),
                 static_cast<std::int64_t>(total), progress_user_data);
      };
      const auto result = runtime->export_frames(
          frames, std::move(config), &message, std::move(progress_bridge));
      if (result != DIGITOR_RESULT_OK)
        write_diagnostic(diagnostic, diagnostic_capacity, message);
      return result;
    } catch (const std::bad_alloc&) {
      write_diagnostic(diagnostic, diagnostic_capacity,
                       "out of memory preparing production export");
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      write_diagnostic(diagnostic, diagnostic_capacity,
                       "unexpected production export adapter failure");
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  static DigitorResult set_preview_target(
      void* user_data, const DigitorFlutterPreviewTarget* target,
      char* diagnostic, std::uint32_t diagnostic_capacity) {
    auto* p = self(user_data);
    if (!p || !target ||
        target->struct_size < sizeof(DigitorFlutterPreviewTarget) ||
        target->api_version != DIGITOR_FLUTTER_PREVIEW_TARGET_VERSION ||
        !target->native_target_handle || !target->width || !target->height ||
        target->handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
        target->handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    std::string message;
    const auto result = p->inputs.preview_target_binder(
        target->native_target_handle, target->width, target->height,
        target->handle_type, message);
    if (result != DIGITOR_RESULT_OK)
      write_diagnostic(diagnostic, diagnostic_capacity, message);
    return result;
  }

  static DigitorResult query_preview(
      void* user_data, DigitorNativePreviewCapabilities* output) {
    auto* p = self(user_data);
    if (!p || !output) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::lock_guard lock(p->mutex);
    auto value = p->inputs.preview_capabilities;
    value.struct_size = sizeof(value);
    value.api_version = DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION;
    value.selected_mode = DIGITOR_PREVIEW_MODE_NATIVE_GPU_STRICT;
    value.native_gpu_preview_available = 1;
    value.cpu_fallback_only = 0;
    *output = value;
    return DIGITOR_RESULT_OK;
  }

  static DigitorResult cancel(void* user_data) {
    auto* p = self(user_data);
    if (!p) return DIGITOR_RESULT_INVALID_ARGUMENT;
    ProductionMediaGraphRuntime* runtime = nullptr;
    {
      std::lock_guard lock(p->mutex);
      runtime = p->runtime.get();
    }
    if (runtime) runtime->cancel();
    return DIGITOR_RESULT_OK;
  }

  static void close_media(void* user_data) {
    auto* p = self(user_data);
    if (!p) return;
    std::lock_guard lock(p->mutex);
    if (p->runtime) p->runtime->cancel();
    p->runtime.reset();
    p->pending_decoder.reset();
    p->bound_graph = nullptr;
    p->bound_identity.clear();
    p->graph_revision = 0;
    p->parameter_revision = 0;
    p->generation = 0;
    p->media_path.clear();
    p->opened = false;
  }

  static void release_texture(
      void* user_data, const DigitorNativeGpuTextureDescriptor* texture) {
    auto* p = self(user_data);
    if (!p || !texture || !texture->generation) return;
    p->inputs.preview_session->consumed(texture->generation);
  }
};

FlutterProductionHostAdapter::FlutterProductionHostAdapter(
    FlutterProductionHostAdapterInputs inputs)
    : impl_(std::make_unique<Impl>(std::move(inputs))) {}
FlutterProductionHostAdapter::~FlutterProductionHostAdapter() = default;

bool FlutterProductionHostAdapter::valid() const noexcept {
  return impl_ && impl_->valid();
}

DigitorFlutterProductionHost FlutterProductionHostAdapter::host() noexcept {
  DigitorFlutterProductionHost value{};
  if (!valid()) return value;
  value.struct_size = sizeof(value);
  value.api_version = DIGITOR_FLUTTER_PRODUCTION_HOST_VERSION;
  value.user_data = impl_.get();
  value.required_device_identity = impl_->inputs.required_device_identity;
  value.required_context_identity = impl_->inputs.required_context_identity;
  value.open_media = &Impl::open_media;
  value.render_frame = &Impl::render_frame;
  value.export_media = &Impl::export_media;
  value.query_preview = &Impl::query_preview;
  value.set_preview_target = &Impl::set_preview_target;
  value.cancel = &Impl::cancel;
  value.close_media = &Impl::close_media;
  value.release_texture = &Impl::release_texture;
  return value;
}

}  // namespace digitor
