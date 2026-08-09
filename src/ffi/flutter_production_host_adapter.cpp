#include "digitor/flutter_production_host_adapter.hpp"

#include "digitor/production_node_graph.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

// Stable C handle layout is shared with the node-graph C API implementation.
struct DigitorNodeGraph { digitor::ProductionNodeGraph impl; };

namespace digitor {
namespace {
constexpr std::uint32_t kDiagnosticCapacity = 512;

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

bool callbacks_complete(const HardwareEncoderCallbacks& callbacks) noexcept {
  return static_cast<bool>(callbacks.open) &&
         static_cast<bool>(callbacks.submit_gpu_frame) &&
         static_cast<bool>(callbacks.drain) &&
         static_cast<bool>(callbacks.finalize_atomic) &&
         static_cast<bool>(callbacks.cancel);
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
  std::uint64_t pending_generation{};
  bool opened{};

  bool valid() const noexcept {
    return static_cast<bool>(inputs.decoder_factory) &&
           static_cast<bool>(inputs.frame_resolver) && inputs.preview_session &&
           static_cast<bool>(inputs.texture_descriptor_builder) &&
           callbacks_complete(inputs.encoder_callbacks) &&
           inputs.encoder_backend != EncoderBackend::software &&
           inputs.fps_num > 0 && inputs.fps_den > 0;
  }

  DigitorResult make_decoder(std::unique_ptr<ProductionHardwareDecodeSession>& out,
                             std::string& diagnostic) {
    out = inputs.decoder_factory(media_path, diagnostic);
    if (!out) {
      if (diagnostic.empty()) diagnostic = "production decoder factory returned no decoder";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    return DIGITOR_RESULT_OK;
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
        parameter_revision == parameter_rev && bound_identity == identity) {
      return DIGITOR_RESULT_OK;
    }

    runtime.reset();
    std::unique_ptr<ProductionHardwareDecodeSession> decoder;
    if (pending_decoder) decoder = std::move(pending_decoder);
    else {
      const auto decoder_result = make_decoder(decoder, diagnostic);
      if (decoder_result != DIGITOR_RESULT_OK) return decoder_result;
    }

    auto presenter = [this](const ProcessedGpuFramePtr& frame,
                            std::string& diagnostic) -> DigitorResult {
      const auto generation = pending_generation;
      if (!generation) {
        diagnostic = "preview generation was not reserved";
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }
      const auto submitted = inputs.preview_session->submit(frame, generation);
      if (!submitted) {
        diagnostic = submitted.diagnostic.empty()
                         ? "native Flutter texture presentation failed"
                         : submitted.diagnostic;
        return submitted.result;
      }
      return DIGITOR_RESULT_OK;
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

  static Impl* self(void* user_data) noexcept {
    return static_cast<Impl*>(user_data);
  }

  static DigitorResult open_media(void* user_data, const char* path,
                                  char* diagnostic,
                                  std::uint32_t diagnostic_capacity) {
    auto* p = self(user_data);
    if (!p || !path || !path[0]) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::string message;
    try {
      std::lock_guard lock(p->mutex);
      if (p->opened) {
        message = "production media is already open";
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return DIGITOR_RESULT_RESOURCE_IN_USE;
      }
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
      message = "out of memory opening production media";
      write_diagnostic(diagnostic, diagnostic_capacity, message);
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      message = "unexpected production media open failure";
      write_diagnostic(diagnostic, diagnostic_capacity, message);
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
        !out_texture || width == 0 || height == 0 || timestamp_us < 0) {
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    std::string message;
    try {
      std::lock_guard lock(p->mutex);
      const auto runtime_result = p->ensure_runtime(
          graph, graph_rev, parameter_rev, message);
      if (runtime_result != DIGITOR_RESULT_OK) {
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return runtime_result;
      }
      const auto generation = ++p->pending_generation;
      ProcessedGpuFramePtr frame;
      FrameNumber frame_number{};
      try { frame_number = p->inputs.frame_resolver(timestamp_us); }
      catch (...) {
        message = "timeline timestamp could not be resolved to a source frame";
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      const auto result = p->runtime->preview(frame_number, &frame, &message);
      if (result != DIGITOR_RESULT_OK) {
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return result;
      }
      DigitorNativeGpuTextureDescriptor descriptor{};
      descriptor.struct_size = sizeof(descriptor);
      descriptor.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
      const auto descriptor_result = p->inputs.texture_descriptor_builder(
          frame, generation, descriptor, message);
      if (descriptor_result != DIGITOR_RESULT_OK) {
        p->inputs.preview_session->consumed(generation);
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return descriptor_result;
      }
      descriptor.struct_size = sizeof(descriptor);
      descriptor.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
      descriptor.width = width;
      descriptor.height = height;
      descriptor.timestamp_us = timestamp_us;
      descriptor.generation = generation;
      *out_texture = descriptor;
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      message = "out of memory rendering production preview";
      write_diagnostic(diagnostic, diagnostic_capacity, message);
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      message = "unexpected production preview adapter failure";
      write_diagnostic(diagnostic, diagnostic_capacity, message);
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
        request->width == 0 || request->height == 0 || request->codec < 0 ||
        request->codec > 3) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::string message;
    try {
      std::lock_guard lock(p->mutex);
      const auto runtime_result = p->ensure_runtime(
          graph, graph_rev, parameter_rev, message);
      if (runtime_result != DIGITOR_RESULT_OK) {
        write_diagnostic(diagnostic, diagnostic_capacity, message);
        return runtime_result;
      }

      std::vector<FrameNumber> frames;
      frames.reserve(static_cast<std::size_t>(
          request->last_frame - request->first_frame + 1));
      for (auto frame = request->first_frame; frame <= request->last_frame; ++frame)
        frames.push_back(frame);

      HardwareEncodeConfig config{};
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
      config.duration_us = static_cast<std::int64_t>(frames.size()) *
          1'000'000LL * p->inputs.fps_den / p->inputs.fps_num;
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
      const auto result = p->runtime->export_frames(
          frames, std::move(config), &message, std::move(progress_bridge));
      if (result != DIGITOR_RESULT_OK)
        write_diagnostic(diagnostic, diagnostic_capacity, message);
      return result;
    } catch (const std::bad_alloc&) {
      message = "out of memory preparing production export";
      write_diagnostic(diagnostic, diagnostic_capacity, message);
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      message = "unexpected production export adapter failure";
      write_diagnostic(diagnostic, diagnostic_capacity, message);
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
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
    std::lock_guard lock(p->mutex);
    if (p->runtime) p->runtime->cancel();
    if (p->inputs.preview_session) p->inputs.preview_session->cancel();
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
    p->media_path.clear();
    p->opened = false;
  }

  static void release_texture(
      void* user_data, const DigitorNativeGpuTextureDescriptor* texture) {
    auto* p = self(user_data);
    if (!p || !texture || texture->generation == 0) return;
    std::lock_guard lock(p->mutex);
    if (p->inputs.preview_session)
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
  value.cancel = &Impl::cancel;
  value.close_media = &Impl::close_media;
  value.release_texture = &Impl::release_texture;
  return value;
}

}  // namespace digitor
