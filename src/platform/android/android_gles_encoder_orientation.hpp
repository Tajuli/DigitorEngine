#pragma once

#if defined(__ANDROID__)

#include "gpu/gpu_backend.hpp"

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <cstdint>

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

namespace digitor::android_gles_export_detail {

// MediaCodec's encoder input window and Flutter's SurfaceProducer target use
// opposite vertical image-origin conventions on the Android GLES path. This
// renderer delegates the real graded-frame presentation to the selected engine
// backend, then performs one GPU-only vertical blit on the *encoder* surface.
// Preview rendering never uses this wrapper, so preview pixels are unchanged.
class EncoderOrientationRenderer final : public IRenderBackend {
 public:
  explicit EncoderOrientationRenderer(IRenderBackend* source) noexcept
      : source_(source) {}

  ~EncoderOrientationRenderer() override { release_resources(); }

  bool initialize(bool) override { return source_ != nullptr; }
  void shutdown() noexcept override { release_resources(); }
  [[nodiscard]] DigitorRendererInfo info() const noexcept override {
    return source_ ? source_->info() : DigitorRendererInfo{};
  }

  void release_resources() noexcept {
    if (!texture_ && !framebuffer_) return;
    const auto previous_display = eglGetCurrentDisplay();
    const auto previous_context = eglGetCurrentContext();
    const auto previous_draw = eglGetCurrentSurface(EGL_DRAW);
    const auto previous_read = eglGetCurrentSurface(EGL_READ);

    bool rebound = false;
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT &&
        surface_ != EGL_NO_SURFACE && previous_context != context_) {
      rebound = eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
    }
    if (eglGetCurrentContext() == context_) {
      if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
      if (texture_) glDeleteTextures(1, &texture_);
    }
    framebuffer_ = 0;
    texture_ = 0;
    width_ = 0;
    height_ = 0;
    if (rebound) {
      if (previous_display != EGL_NO_DISPLAY &&
          previous_context != EGL_NO_CONTEXT) {
        (void)eglMakeCurrent(previous_display, previous_draw, previous_read,
                             previous_context);
      } else {
        (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
      }
    }
  }

 protected:
  DigitorResult execute_present_gpu_frame(
      const ProcessedGpuFramePtr& frame) noexcept override {
    if (!source_ || !frame) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto result = source_->present_gpu_frame(frame);
    if (result != DIGITOR_RESULT_OK) return result;

    const auto display = eglGetCurrentDisplay();
    const auto context = eglGetCurrentContext();
    const auto surface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT ||
        surface == EGL_NO_SURFACE) {
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    const auto& metadata = frame->metadata();
    if (!metadata.width || !metadata.height ||
        !ensure_target(display, context, surface, metadata.width,
                       metadata.height)) {
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
    const auto srgb_was_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    // The delegated present has already produced the display-transformed
    // default framebuffer. Blit encoded values without a second sRGB transform.
    if (srgb_was_enabled) glDisable(GL_FRAMEBUFFER_SRGB_EXT);

    const auto width = static_cast<GLint>(metadata.width);
    const auto height = static_cast<GLint>(metadata.height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height, 0, height, width, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glFlush();
    const auto error = glGetError();

    glBindFramebuffer(GL_READ_FRAMEBUFFER,
                      static_cast<GLuint>(previous_read_fbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      static_cast<GLuint>(previous_draw_fbo));
    if (srgb_was_enabled) glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    return error == GL_NO_ERROR ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

 private:
  bool ensure_target(EGLDisplay display, EGLContext context, EGLSurface surface,
                     std::uint32_t width, std::uint32_t height) noexcept {
    if (context_ != EGL_NO_CONTEXT && context_ != context) release_resources();
    display_ = display;
    context_ = context;
    surface_ = surface;

    if (!texture_) glGenTextures(1, &texture_);
    if (!framebuffer_) glGenFramebuffers(1, &framebuffer_);
    if (!texture_ || !framebuffer_) return false;
    if (width_ == width && height_ == height) return true;

    GLint previous_texture = 0;
    GLint previous_draw_fbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture_, 0);
    const bool complete =
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      static_cast<GLuint>(previous_draw_fbo));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    if (!complete) return false;
    width_ = width;
    height_ = height;
    return true;
  }

  IRenderBackend* source_{};
  EGLDisplay display_{EGL_NO_DISPLAY};
  EGLContext context_{EGL_NO_CONTEXT};
  EGLSurface surface_{EGL_NO_SURFACE};
  GLuint texture_{};
  GLuint framebuffer_{};
  std::uint32_t width_{};
  std::uint32_t height_{};
};

}  // namespace digitor::android_gles_export_detail

#endif  // defined(__ANDROID__)
