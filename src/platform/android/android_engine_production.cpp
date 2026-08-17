#include "platform/android/android_engine_production.hpp"

#include <cstdint>
#include <exception>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

#if defined(__ANDROID__)
#include "digitor/android_mediacodec_decoder.hpp"
#include "digitor/media.hpp"
#include "digitor/production_hardware_decode.hpp"
#endif

namespace digitor {
namespace {

struct DependencyFactoryState final {
  std::mutex mutex;
  AndroidEngineProductionDependenciesFactory factory;
};

DependencyFactoryState& dependency_factory_state() {
  static DependencyFactoryState state;
  return state;
}

bool complete_encoder(const AndroidHardwareEncoderHost& host) noexcept {
  return host.open && host.submit && host.drain && host.finalize_mp4_atomic &&
         host.cancel && host.qualification;
}

bool complete_dependencies(
    const AndroidEngineProductionDependencies& dependencies) noexcept {
  return dependencies.timeline.create_target &&
         dependencies.timeline.execute_effects &&
         dependencies.timeline.composite_layer &&
         dependencies.timeline.frame_evictable &&
         complete_encoder(dependencies.encoder) &&
         dependencies.decoder_factory && dependencies.frame_resolver &&
         dependencies.texture_descriptor_builder &&
         dependencies.preview_target_binder && dependencies.fps_num > 0 &&
         dependencies.fps_den > 0 && dependencies.video_bitrate > 0 &&
         !dependencies.package_identity.empty() &&
         !dependencies.build_identity.empty();
}

bool capability_resources_complete(const BackendProductionCapability& backend,
                                   std::string& diagnostic) noexcept {
  if (!backend.valid()) {
    diagnostic = "selected Android backend has no live production capability";
    return false;
  }
  if (backend.backend == DIGITOR_RENDERER_VULKAN) {
    const auto* resources =
        std::get_if<VulkanProductionResources>(&backend.resources);
    if (!resources || !resources->instance || !resources->physical_device ||
        !resources->device || !resources->queue ||
        !backend.native_media_import) {
      diagnostic =
          "Android Vulkan production capability is missing device/queue or renderer-owned AHardwareBuffer import";
      return false;
    }
    return true;
  }
  if (backend.backend == DIGITOR_RENDERER_OPENGL_ES) {
    const auto* resources =
        std::get_if<GlesProductionResources>(&backend.resources);
    if (!resources || !resources->egl_display || !resources->egl_context ||
        !backend.native_media_import) {
      diagnostic =
          "Android GLES production capability is missing EGL ownership or renderer-owned AHardwareBuffer import";
      return false;
    }
    return true;
  }
  diagnostic = "Android production requires Vulkan or OpenGL ES";
  return false;
}

#if defined(__ANDROID__)

AndroidMediaCodecSessionConfig decoder_config(const std::string& media_path) {
  AndroidMediaCodecSessionConfig config{};
  config.media_path = media_path;
  config.max_acquired_images = 6;
  config.dequeue_timeout_us = 10'000;
  config.scheduling = AndroidDecodeScheduling::frame_accurate;
  config.strict_zero_copy = true;
  return config;
}

PixelFormat media_pixel_format(NativeMediaPixelFormat value) {
  switch (value) {
    case NativeMediaPixelFormat::nv12:
      return PixelFormat::nv12;
    case NativeMediaPixelFormat::p010:
      return PixelFormat::p010;
    default:
      throw std::runtime_error(
          "Android MediaCodec production decoder returned unsupported GPU pixel format");
  }
}

class AndroidProductionVideoDecoder final : public VideoDecoder {
 public:
  explicit AndroidProductionVideoDecoder(const std::string& media_path)
      : decoder_(decoder_config(media_path)), cache_(8) {
    if (media_path.empty())
      throw std::invalid_argument("Android production media path is required");
    const auto result = decoder_.initialize();
    if (result != DIGITOR_RESULT_OK) {
      const auto diagnostic = decoder_.diagnostic();
      throw std::runtime_error(
          diagnostic.empty()
              ? "Android NDK MediaCodec production decoder initialization failed"
              : diagnostic);
    }
  }

  ~AndroidProductionVideoDecoder() override = default;

  std::shared_ptr<VideoFrame> decode(FrameNumber frame_number) override {
    if (frame_number < 0) throw std::out_of_range("negative frame");
    if (auto cached = cache_.get(frame_number)) return cached;
    if (frame_number < next_number_)
      throw std::out_of_range(
          "Android production frame is no longer cached; seek before decoding it again");

    std::shared_ptr<VideoFrame> result;
    while (next_number_ <= frame_number) {
      NativeMediaSurfacePtr surface;
      const auto decode_result = decoder_.decode_next(surface);
      if (decode_result != DIGITOR_RESULT_OK) {
        if (decoder_.statistics().eos_drained) return {};
        const auto diagnostic = decoder_.diagnostic();
        throw std::runtime_error(
            diagnostic.empty()
                ? "Android NDK MediaCodec production decode failed"
                : diagnostic);
      }
      if (!surface)
        throw std::runtime_error(
            "Android NDK MediaCodec returned no AHardwareBuffer surface");

      const auto& descriptor = surface->descriptor();
      if (descriptor.platform != NativeMediaPlatform::android ||
          descriptor.handle_type != NativeMediaHandleType::ahardware_buffer ||
          !descriptor.native_handle || !descriptor.width ||
          !descriptor.height || descriptor.plane_count != 2) {
        throw std::runtime_error(
            "Android NDK MediaCodec returned an invalid production AHardwareBuffer descriptor");
      }

      auto frame = std::make_shared<VideoFrame>();
      frame->number = next_number_++;
      frame->pts = descriptor.timestamp_us;
      frame->duration = 0;
      frame->width = descriptor.width;
      frame->height = descriptor.height;
      frame->pixel_format = media_pixel_format(descriptor.pixel_format);
      frame->color.primaries = descriptor.color.primaries;
      frame->color.transfer = descriptor.color.transfer;
      frame->color.matrix = descriptor.color.matrix;
      frame->color.range = descriptor.color.full_range
                               ? ColorRange::full
                               : ColorRange::limited;
      frame->native_surface = std::move(surface);
      cache_.put(frame->number, frame);
      result = std::move(frame);
    }
    return result;
  }

  void seek(std::int64_t pts_us) override {
    if (pts_us < 0) throw std::out_of_range("negative timestamp");
    const auto result = decoder_.seek(pts_us);
    if (result != DIGITOR_RESULT_OK) {
      const auto diagnostic = decoder_.diagnostic();
      throw std::runtime_error(
          diagnostic.empty() ? "Android MediaCodec seek failed" : diagnostic);
    }
    cache_.clear();
    next_number_ = 0;
  }

  DecoderInfo info() const override {
    return {HardwareDecode::mediacodec,
            true,
            "Android NDK MediaCodec/AImageReader AHardwareBuffer",
            true,
            NativeMediaHandleType::ahardware_buffer};
  }

 private:
  AndroidMediaCodecAhbDecoder decoder_;
  FrameCache<VideoFrame> cache_;
  FrameNumber next_number_{};
};

ProductionDecoderFactory make_engine_android_decoder_factory(
    const BackendProductionCapability& backend) {
  if (!backend.native_media_import ||
      (backend.backend != DIGITOR_RENDERER_VULKAN &&
       backend.backend != DIGITOR_RENDERER_OPENGL_ES)) {
    return {};
  }

  const auto renderer_backend = backend.backend;
  auto importer = backend.native_media_import;
  return [renderer_backend, importer = std::move(importer)](
             const std::string& media_path,
             std::string& diagnostic)
             -> std::unique_ptr<ProductionHardwareDecodeSession> {
    try {
      auto decoder =
          std::make_unique<AndroidProductionVideoDecoder>(media_path);
      ProductionHardwareDecodeOptions options{};
      options.renderer_backend = renderer_backend;
      options.render_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
      options.require_zero_copy = true;
      options.require_monotonic_timestamps = true;
      auto session = std::make_unique<ProductionHardwareDecodeSession>(
          std::move(decoder), importer, options);
      diagnostic.clear();
      return session;
    } catch (const std::bad_alloc&) {
      diagnostic =
          "out of memory opening Android NDK MediaCodec production decoder";
    } catch (const std::exception& error) {
      diagnostic = error.what();
    } catch (...) {
      diagnostic =
          "unexpected Android NDK MediaCodec production decoder initialization failure";
    }
    return {};
  };
}

#endif  // defined(__ANDROID__)

}  // namespace

AndroidEngineProductionBuildResult assemble_android_engine_production_build(
    const BackendProductionCapability& backend,
    const FlutterProductionPluginAttachment& attachment,
    AndroidEngineProductionDependencies dependencies) noexcept {
  AndroidEngineProductionBuildResult out{};
  try {
    if (attachment.platform != DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID ||
        !attachment.flutter_texture_registrar ||
        attachment.implementation_identity.empty()) {
      out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
      out.diagnostic = "valid Android Flutter production attachment is required";
      return out;
    }
    if (!capability_resources_complete(backend, out.diagnostic)) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      return out;
    }

#if defined(__ANDROID__)
    // Decode/import belongs to the selected Android renderer generation.  The
    // Flutter app and the platform dependency payload must not substitute an
    // FFmpeg MediaCodec decoder, CPU frame path, or foreign GPU importer.
    dependencies.decoder_factory =
        make_engine_android_decoder_factory(backend);
#endif

    if (!complete_dependencies(dependencies)) {
      out.result = DIGITOR_RESULT_NOT_INITIALIZED;
      out.diagnostic =
          "engine-owned Android production dependencies are incomplete";
      return out;
    }
    if (dependencies.timeline.backend != backend.backend ||
        dependencies.timeline.context_identity !=
            backend.frame_context_identity) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.diagnostic =
          "Android timeline is not bound to selected backend generation";
      return out;
    }
    if (!dependencies.preview_capabilities.native_gpu_preview_available ||
        dependencies.preview_capabilities.cpu_fallback_only) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.diagnostic = "Android preview is not native-GPU ready";
      return out;
    }

    AndroidNativeProviderBindings bindings{};
    bindings.timeline = dependencies.timeline;
    bindings.flutter.flutter_texture_registrar =
        attachment.flutter_texture_registrar;
    bindings.flutter.implementation_identity =
        attachment.implementation_identity;
    bindings.flutter.attached = [] { return true; };
    bindings.flutter.present =
        [attachment, presenter = dependencies.flutter_present](
            const ProcessedGpuFramePtr& frame,
            std::uint64_t generation) mutable {
          if (!presenter) return DIGITOR_RESULT_NOT_INITIALIZED;
          std::string diagnostic;
          return presenter(attachment, frame, generation, diagnostic);
        };
    bindings.encoder = dependencies.encoder;
    bindings.capabilities = dependencies.capabilities;
    bindings.device_identity = backend.frame_context_identity;
    bindings.package_identity = dependencies.package_identity;
    bindings.build_identity = dependencies.build_identity;

#if !defined(__ANDROID__)
    out.result = DIGITOR_RESULT_UNSUPPORTED;
    out.diagnostic = "Android production assembly requires Android";
    return out;
#else
    auto provider = create_android_native_provider(std::move(bindings));
    if (!provider) {
      out.result = provider.result;
      out.diagnostic = provider.diagnostic;
      return out;
    }

    FlutterProductionProviderBuild build{};
    build.provider = std::move(provider.provider);
    build.platform_inputs.platform = ProductionPlatform::android;
    build.platform_inputs.timeline = dependencies.timeline;
    build.decoder_factory = std::move(dependencies.decoder_factory);
    build.frame_resolver = std::move(dependencies.frame_resolver);
    build.texture_descriptor_builder =
        std::move(dependencies.texture_descriptor_builder);
    build.preview_target_binder =
        std::move(dependencies.preview_target_binder);
    build.preview_capabilities = dependencies.preview_capabilities;
    build.encoder_backend = dependencies.encoder_backend;
    build.fps_num = dependencies.fps_num;
    build.fps_den = dependencies.fps_den;
    build.video_bitrate = dependencies.video_bitrate;
    build.required_device_identity = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(backend.frame_context_identity));
    build.required_context_identity = backend.context_identity;

    out.build = std::move(build);
    out.result = DIGITOR_RESULT_OK;
    return out;
#endif
  } catch (const std::bad_alloc&) {
    out.result = DIGITOR_RESULT_OUT_OF_MEMORY;
    out.diagnostic =
        "out of memory assembling Android production provider";
  } catch (const std::exception& error) {
    out.result = DIGITOR_RESULT_INTERNAL_ERROR;
    out.diagnostic = error.what();
  } catch (...) {
    out.result = DIGITOR_RESULT_INTERNAL_ERROR;
    out.diagnostic = "Android production assembly failed";
  }
  return out;
}

DigitorResult install_android_engine_production_dependencies_factory(
    AndroidEngineProductionDependenciesFactory factory,
    std::string* diagnostic) noexcept {
  if (!factory) {
    if (diagnostic)
      *diagnostic = "Android production dependency factory is required";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  try {
    auto& state = dependency_factory_state();
    {
      std::scoped_lock lock(state.mutex);
      if (state.factory) {
        if (diagnostic)
          *diagnostic =
              "Android production dependency factory already installed";
        return DIGITOR_RESULT_RESOURCE_IN_USE;
      }
      state.factory = std::move(factory);
    }

    // Dependency installation and Flutter production-host registration are
    // separate lifecycle events. The Flutter attachment can arrive before the
    // selected backend installs its provider builder, so a deferred retry must
    // never discard a valid engine-owned Android dependency factory. The
    // pending attachment is retried when the production runtime is available.
    std::string retry_diagnostic;
    const auto retry = retry_flutter_production_host_registration(
        DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID, &retry_diagnostic);
    if (retry != DIGITOR_RESULT_OK) {
      if (diagnostic) {
        *diagnostic = retry_diagnostic.empty()
                          ? "Android production dependency factory installed; Flutter host registration is pending"
                          : std::move(retry_diagnostic);
      }
      return DIGITOR_RESULT_OK;
    }
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    if (diagnostic)
      *diagnostic = "failed to install Android production dependency factory";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult clear_android_engine_production_dependencies_factory() noexcept {
  auto& state = dependency_factory_state();
  std::scoped_lock lock(state.mutex);
  state.factory = {};
  return DIGITOR_RESULT_OK;
}

std::unique_ptr<ProductionIntegrationRuntime>
install_android_engine_production_runtime(
    const BackendProductionCapability& backend,
    std::string* diagnostic) noexcept {
  std::string capability_diagnostic;
  if (!capability_resources_complete(backend, capability_diagnostic)) {
    if (diagnostic) *diagnostic = std::move(capability_diagnostic);
    return {};
  }

  return ProductionIntegrationRuntime::install(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID,
      [backend](const FlutterProductionPluginAttachment& attachment,
                std::string& local)
          -> std::optional<FlutterProductionProviderBuild> {
        AndroidEngineProductionDependenciesFactory factory;
        {
          auto& state = dependency_factory_state();
          std::scoped_lock lock(state.mutex);
          factory = state.factory;
        }
        if (!factory) {
          local =
              "engine-owned Android production dependencies are not installed";
          return std::nullopt;
        }
        auto dependencies = factory(backend, attachment, local);
        if (!dependencies) {
          if (local.empty())
            local =
                "engine-owned Android production dependencies are unavailable";
          return std::nullopt;
        }
        auto assembled = assemble_android_engine_production_build(
            backend, attachment, std::move(*dependencies));
        if (!assembled) {
          local = assembled.diagnostic;
          return std::nullopt;
        }
        local.clear();
        return std::move(assembled.build);
      },
      diagnostic);
}

}  // namespace digitor

#if defined(__ANDROID__)
#include "android_native_provider.cpp"
#endif