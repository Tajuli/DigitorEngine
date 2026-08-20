#pragma once

#if defined(__ANDROID__)

#include "digitor/flutter_production_provider_builder.hpp"
#include "digitor/native_preview_presentation.hpp"
#include "gpu/backend_production_capability.hpp"
#include "gpu/gpu_backend.hpp"
#include "android_gles_mediacodec_encoder.hpp"
#include "android_gles_encoder_orientation.hpp"
#include "android_source_audio_mux.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <android/native_window.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

namespace digitor {
namespace android_gles_flutter_preview_detail {

inline bool extension_present(const char* extensions, const char* needle) noexcept {
  if (!extensions || !needle || !*needle) return false;
  const auto needle_size = std::strlen(needle);
  const char* cursor = extensions;
  while ((cursor = std::strstr(cursor, needle)) != nullptr) {
    const bool left = cursor == extensions || cursor[-1] == ' ';
    const char tail = cursor[needle_size];
    const bool right = tail == '\0' || tail == ' ';
    if (left && right) return true;
    cursor += needle_size;
  }
  return false;
}

class AndroidGlesFlutterTarget final {
 public:
  AndroidGlesFlutterTarget(EGLDisplay display, EGLContext context,
                           IRenderBackend* renderer) noexcept
      : display_(display), context_(context), renderer_(renderer) {}

  ~AndroidGlesFlutterTarget() { clear_locked(); }

  DigitorResult bind(std::uint64_t native_target_handle,
                     std::uint32_t width,
                     std::uint32_t height,
                     std::int32_t handle_type,
                     std::string& diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    if (!native_target_handle || !width || !height) {
      diagnostic = "Android Flutter SurfaceProducer target is invalid";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (handle_type != DIGITOR_NATIVE_TEXTURE_HANDLE_GL_TEXTURE) {
      diagnostic =
          "Android GLES production preview requires the GL render-target delivery contract";
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    if (display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT || !renderer_) {
      diagnostic = "Android GLES production context is unavailable";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    auto* next_window = reinterpret_cast<ANativeWindow*>(
        static_cast<std::uintptr_t>(native_target_handle));
    if (next_window == window_ && surface_ != EGL_NO_SURFACE &&
        width_ == width && height_ == height) {
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    }

    EGLint config_id = 0;
    if (eglQueryContext(display_, context_, EGL_CONFIG_ID, &config_id) != EGL_TRUE) {
      diagnostic = "failed to query the selected GLES EGLConfig";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    const EGLint choose_attributes[] = {EGL_CONFIG_ID, config_id, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (eglChooseConfig(display_, choose_attributes, &config, 1, &count) != EGL_TRUE ||
        count != 1 || !config) {
      diagnostic = "failed to resolve the selected GLES EGLConfig";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    EGLint surface_type = 0;
    if (eglGetConfigAttrib(display_, config, EGL_SURFACE_TYPE, &surface_type) != EGL_TRUE ||
        (surface_type & EGL_WINDOW_BIT) == 0) {
      diagnostic =
          "selected GLES EGLConfig cannot present to an Android native window";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    const char* egl_extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (!extension_present(egl_extensions, "EGL_KHR_gl_colorspace")) {
      diagnostic =
          "Android GLES production preview requires EGL_KHR_gl_colorspace for the display transform";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    const EGLint surface_attributes[] = {
        EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SRGB_KHR, EGL_NONE};
    ANativeWindow_acquire(next_window);
    const auto next_surface =
        eglCreateWindowSurface(display_, config, next_window,
                               surface_attributes);
    if (next_surface == EGL_NO_SURFACE) {
      ANativeWindow_release(next_window);
      diagnostic = "failed to create the Flutter SurfaceProducer EGL window surface";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    clear_locked();
    window_ = next_window;
    surface_ = next_surface;
    width_ = width;
    height_ = height;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

  DigitorResult present(const ProcessedGpuFramePtr& frame,
                        std::string& diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    if (!frame || !renderer_ || !window_ || surface_ == EGL_NO_SURFACE) {
      diagnostic = "Android Flutter production preview target is not bound";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    const auto previous_display = eglGetCurrentDisplay();
    const auto previous_context = eglGetCurrentContext();
    const auto previous_draw = eglGetCurrentSurface(EGL_DRAW);
    const auto previous_read = eglGetCurrentSurface(EGL_READ);

    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
      diagnostic = "failed to bind the Flutter SurfaceProducer EGL target";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    const char* gl_extensions =
        reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!extension_present(gl_extensions, "GL_EXT_sRGB_write_control")) {
      restore(previous_display, previous_draw, previous_read, previous_context);
      diagnostic =
          "Android GLES production preview requires GL_EXT_sRGB_write_control for the display transform";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    const auto srgb_was_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    auto result = renderer_->present_gpu_frame(frame);
    if (result == DIGITOR_RESULT_OK && eglSwapBuffers(display_, surface_) != EGL_TRUE)
      result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if (!srgb_was_enabled) glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    restore(previous_display, previous_draw, previous_read, previous_context);

    if (result != DIGITOR_RESULT_OK) {
      diagnostic = "processed GLES frame could not be presented to Flutter SurfaceProducer";
      return result;
    }
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

 private:
  void restore(EGLDisplay display, EGLSurface draw, EGLSurface read,
               EGLContext context) noexcept {
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
      (void)eglMakeCurrent(display, draw, read, context);
    } else {
      (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
    }
  }

  void clear_locked() noexcept {
    if (surface_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY) {
      eglDestroySurface(display_, surface_);
      surface_ = EGL_NO_SURFACE;
    }
    if (window_) {
      ANativeWindow_release(window_);
      window_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
  }

  std::mutex mutex_;
  EGLDisplay display_{EGL_NO_DISPLAY};
  EGLContext context_{EGL_NO_CONTEXT};
  IRenderBackend* renderer_{};
  ANativeWindow* window_{};
  EGLSurface surface_{EGL_NO_SURFACE};
  std::uint32_t width_{};
  std::uint32_t height_{};
};

class AndroidGlesFlutterPreviewHost final : public NativePreviewTextureHost {
 public:
  AndroidGlesFlutterPreviewHost(
      const void* registrar,
      const void* frame_context_identity,
      std::shared_ptr<AndroidGlesFlutterTarget> target) noexcept
      : registrar_(registrar),
        frame_context_identity_(frame_context_identity),
        target_(std::move(target)) {}

  [[nodiscard]] bool attached() const noexcept override {
    return registrar_ != nullptr && target_ != nullptr;
  }
  [[nodiscard]] DigitorRendererBackend backend() const noexcept override {
    return DIGITOR_RENDERER_OPENGL_ES;
  }
  [[nodiscard]] const void* device_identity() const noexcept override {
    return frame_context_identity_;
  }
  [[nodiscard]] bool deferred_display_transform() const noexcept override {
    return true;
  }
  DigitorResult present(const ProcessedGpuFramePtr& frame,
                        std::uint64_t generation) noexcept override {
    if (!attached() || generation == 0) return DIGITOR_RESULT_NOT_INITIALIZED;
    std::string diagnostic;
    return target_->present(frame, diagnostic);
  }

 private:
  const void* registrar_{};
  const void* frame_context_identity_{};
  std::shared_ptr<AndroidGlesFlutterTarget> target_;
};

}  // namespace android_gles_flutter_preview_detail

inline std::optional<FlutterProductionProviderBuild>
make_android_gles_flutter_preview_build(
    const BackendProductionCapability& backend,
    const FlutterProductionPluginAttachment& attachment,
    ProductionDecoderFactory decoder_factory,
    std::string& diagnostic) {
  using namespace android_gles_flutter_preview_detail;
  if (attachment.platform != DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID ||
      !attachment.flutter_texture_registrar ||
      attachment.implementation_identity.empty()) {
    diagnostic = "valid Android Flutter production attachment is required";
    return std::nullopt;
  }
  if (backend.backend != DIGITOR_RENDERER_OPENGL_ES ||
      !backend.frame_context_identity || backend.context_identity == 0 ||
      !backend.native_media_import || !decoder_factory) {
    diagnostic =
        "engine-owned Android GLES preview requires a live renderer and MediaCodec AHardwareBuffer import";
    return std::nullopt;
  }
  const auto* resources = std::get_if<GlesProductionResources>(&backend.resources);
  if (!resources || !resources->egl_display || !resources->egl_context) {
    diagnostic = "engine-owned Android GLES preview has no live EGL context";
    return std::nullopt;
  }

  auto* renderer = const_cast<IRenderBackend*>(
      static_cast<const IRenderBackend*>(backend.frame_context_identity));
  const auto egl_display = static_cast<EGLDisplay>(resources->egl_display);
  const auto egl_context = static_cast<EGLContext>(resources->egl_context);
  auto target = std::make_shared<AndroidGlesFlutterTarget>(
      egl_display, egl_context, renderer);
  auto host = std::make_shared<AndroidGlesFlutterPreviewHost>(
      attachment.flutter_texture_registrar, backend.frame_context_identity,
      target);

  FlutterProductionProviderBuild build{};
  build.provider.platform = ProductionPlatform::android;
  build.platform_inputs.platform = ProductionPlatform::android;
  build.preview_session =
      std::make_shared<NativePreviewPresentationSession>(std::move(host));
  build.decoder_factory = std::move(decoder_factory);
  build.preview_target_binder =
      [target](std::uint64_t handle, std::uint32_t width,
               std::uint32_t height, std::int32_t handle_type,
               std::string& local) {
        return target->bind(handle, width, height, handle_type, local);
      };
  build.texture_descriptor_builder =
      [backend](const ProcessedGpuFramePtr& frame, std::uint64_t generation,
                DigitorNativeGpuTextureDescriptor& out,
                std::string& local) {
        out = {};
        if (!frame || !frame->ready() || !frame->context_live() ||
            frame->backend() != DIGITOR_RENDERER_OPENGL_ES || generation == 0 ||
            !frame->has_context_identity(backend.frame_context_identity)) {
          local = "Android GLES preview descriptor received an incompatible processed frame";
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }
        const auto& metadata = frame->metadata();
        out.struct_size = sizeof(out);
        out.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
        out.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_OPENGL_ES;
        out.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_GL_TEXTURE;
        // Android render-target delivery never imports this field in Flutter.
        // Use the engine frame identity as the non-zero completion/ownership
        // token required by the common production C ABI; the pixels were
        // already rendered into the bound SurfaceProducer target by the host.
        out.native_handle = frame->identity();
        out.width = metadata.width;
        out.height = metadata.height;
        out.pixel_format = metadata.format;
        out.alpha_mode = static_cast<std::uint32_t>(metadata.alpha);
        out.timestamp_us = metadata.timestamp;
        out.generation = generation;
        out.device_identity = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(backend.frame_context_identity));
        out.context_identity = backend.context_identity;
        out.ownership_token = frame->identity();
        out.readiness = DIGITOR_NATIVE_TEXTURE_READY;
        local.clear();
        return DIGITOR_RESULT_OK;
      };

  auto& preview = build.preview_capabilities;
  preview.struct_size = sizeof(preview);
  preview.api_version = DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION;
  preview.native_gpu_preview_available = 1;
  preview.true_shared_resource_zero_copy = 0;
  preview.gpu_to_gpu_copy = 1;
  preview.cpu_fallback_only = 0;
  preview.sdr_supported = 1;
  preview.hdr_supported = 0;
  preview.protected_content_supported = 0;
  preview.resize_supported = 1;
  preview.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_OPENGL_ES;
  preview.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_GL_TEXTURE;
  preview.supported_pixel_formats =
      (std::uint64_t{1} << DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT) |
      (std::uint64_t{1} << DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT);
  preview.selected_mode = DIGITOR_PREVIEW_MODE_NATIVE_GPU_STRICT;

  build.encoder_factory =
      [egl_display, egl_context, renderer,
       frame_context_identity = backend.frame_context_identity](
          std::shared_ptr<const ExportRenderSnapshot> snapshot) mutable {
        using android_gles_export_detail::EncoderOrientationRenderer;
        auto orientation =
            std::make_shared<EncoderOrientationRenderer>(renderer);
        auto host =
            android_gles_mediacodec_detail::make_android_gles_mediacodec_host(
                egl_display, egl_context, orientation.get(),
                frame_context_identity);

        // Release the export-only blit resources while MediaCodec's EGL
        // surface still exists. Keeping the shared renderer in every callback
        // also guarantees that the raw pointer owned by the encoder remains
        // valid for the complete session.
        auto inner_finalize = host.finalize_mp4_atomic;
        host.finalize_mp4_atomic =
            [orientation, inner_finalize](std::string& local) mutable {
              orientation->release_resources();
              return inner_finalize(local);
            };
        auto inner_cancel = host.cancel;
        host.cancel = [orientation, inner_cancel]() mutable {
          orientation->release_resources();
          inner_cancel();
        };
        auto keep_orientation = orientation;
        auto inner_qualification = host.qualification;
        host.qualification =
            [keep_orientation, inner_qualification]() mutable {
              return inner_qualification();
            };

        host = android_source_audio_mux_detail::wrap_source_audio_mux(
            std::move(host), snapshot);
        return create_production_encoder(
            ProductionPlatform::android, std::move(snapshot), {},
            std::move(host), {}, {});
      };
  build.encoder_backend = EncoderBackend::media_codec;
  build.required_device_identity = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(backend.frame_context_identity));
  build.required_context_identity = backend.context_identity;
  diagnostic.clear();
  return build;
}

}  // namespace digitor

#endif  // defined(__ANDROID__)
