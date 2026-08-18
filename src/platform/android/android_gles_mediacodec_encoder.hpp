#pragma once

#if defined(__ANDROID__)

#include "digitor/android_hardware_encode_adapter.hpp"
#include "digitor/production_platform_integration.hpp"
#include "gpu/gpu_backend.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

namespace digitor {
namespace android_gles_mediacodec_detail {

constexpr std::int32_t kColorFormatSurface = 0x7F000789;

using CreateInputSurfaceFn = media_status_t (*)(AMediaCodec*, ANativeWindow**);
using SignalEndOfInputStreamFn = media_status_t (*)(AMediaCodec*);
using GetCodecNameFn = media_status_t (*)(AMediaCodec*, char**);
using ReleaseCodecNameFn = void (*)(AMediaCodec*, char*);
using DeviceApiLevelFn = int (*)();

inline CreateInputSurfaceFn create_input_surface_fn() noexcept {
  static const auto fn = reinterpret_cast<CreateInputSurfaceFn>(
      dlsym(RTLD_DEFAULT, "AMediaCodec_createInputSurface"));
  return fn;
}

inline SignalEndOfInputStreamFn signal_end_of_input_stream_fn() noexcept {
  static const auto fn = reinterpret_cast<SignalEndOfInputStreamFn>(
      dlsym(RTLD_DEFAULT, "AMediaCodec_signalEndOfInputStream"));
  return fn;
}

inline GetCodecNameFn get_codec_name_fn() noexcept {
  static const auto fn = reinterpret_cast<GetCodecNameFn>(
      dlsym(RTLD_DEFAULT, "AMediaCodec_getName"));
  return fn;
}

inline ReleaseCodecNameFn release_codec_name_fn() noexcept {
  static const auto fn = reinterpret_cast<ReleaseCodecNameFn>(
      dlsym(RTLD_DEFAULT, "AMediaCodec_releaseName"));
  return fn;
}

inline int device_api_level() noexcept {
  static const auto fn = reinterpret_cast<DeviceApiLevelFn>(
      dlsym(RTLD_DEFAULT, "android_get_device_api_level"));
  return fn ? fn() : 0;
}

inline bool extension_present(const char* extensions,
                              const char* needle) noexcept {
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

inline bool software_codec_name(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name.find("c2.android.") != std::string::npos ||
         name.find("omx.google.") != std::string::npos ||
         name.find("software") != std::string::npos ||
         name.find("ffmpeg") != std::string::npos ||
         name.find(".sw.") != std::string::npos;
}

class AndroidGlesMediaCodecEncoder final {
 public:
  AndroidGlesMediaCodecEncoder(EGLDisplay display, EGLContext context,
                               IRenderBackend* renderer,
                               const void* frame_context_identity) noexcept
      : display_(display),
        context_(context),
        renderer_(renderer),
        frame_context_identity_(frame_context_identity) {}

  ~AndroidGlesMediaCodecEncoder() { cancel(); }

  DigitorResult open(const HardwareEncodeConfig& config,
                     const ExportRenderSnapshot& snapshot,
                     AndroidHardwareEncodeCapabilities& capabilities,
                     std::string& diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    if (opened_) {
      diagnostic = "Android MediaCodec encoder is already open";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    if (display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT || !renderer_ ||
        !frame_context_identity_) {
      diagnostic = "Android GLES encoder has no live renderer context";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    if (snapshot.renderer_backend() != DIGITOR_RENDERER_OPENGL_ES ||
        snapshot.encoder_backend() != EncoderBackend::media_codec ||
        config.backend != EncoderBackend::media_codec) {
      diagnostic = "Android GLES export requires the MediaCodec/GLES contract";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    api_level_ = device_api_level();
    const auto create_surface = create_input_surface_fn();
    signal_eos_ = signal_end_of_input_stream_fn();
    if (api_level_ < 26 || !create_surface || !signal_eos_) {
      diagnostic = "Android API 26+ MediaCodec input-surface export is unavailable";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    const auto codec = snapshot.data().profile.codec;
    const char* mime = nullptr;
    switch (codec) {
      case ExportCodec::h264:
        mime = "video/avc";
        break;
      case ExportCodec::hevc:
        mime = "video/hevc";
        break;
      case ExportCodec::av1:
      case ExportCodec::prores:
        diagnostic = "Android MediaCodec export currently supports H.264 and HEVC";
        return DIGITOR_RESULT_UNSUPPORTED;
    }

    codec_ = AMediaCodec_createEncoderByType(mime);
    if (!codec_) {
      diagnostic = "Android could not create the requested MediaCodec encoder";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    codec_name_ = codec_component_name_locked();
    if (codec_name_.empty()) {
      diagnostic =
          "Android hardware encoder identity could not be verified on this device";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    if (software_codec_name(codec_name_)) {
      diagnostic = "Android selected a software MediaCodec encoder; GPU export remains fail-closed";
      cleanup_locked(false);
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    AMediaFormat* format = AMediaFormat_new();
    if (!format) {
      diagnostic = "failed to allocate Android encoder format";
      cleanup_locked(false);
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    AMediaFormat_setString(format, "mime", mime);
    AMediaFormat_setInt32(format, "width", config.profile.width);
    AMediaFormat_setInt32(format, "height", config.profile.height);
    AMediaFormat_setInt32(
        format, "bitrate",
        static_cast<std::int32_t>(std::min<std::int64_t>(
            config.profile.video_bitrate, std::numeric_limits<std::int32_t>::max())));
    const auto fps = config.profile.fps_den > 0
                         ? static_cast<float>(config.profile.fps_num) /
                               static_cast<float>(config.profile.fps_den)
                         : 30.0f;
    AMediaFormat_setFloat(format, "frame-rate", fps);
    AMediaFormat_setInt32(format, "i-frame-interval", 1);
    AMediaFormat_setInt32(format, "color-format", kColorFormatSurface);

    const auto configure = AMediaCodec_configure(
        codec_, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);
    if (configure != AMEDIA_OK) {
      diagnostic = "MediaCodec rejected the hardware encoder configuration";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    ANativeWindow* input_window = nullptr;
    if (create_surface(codec_, &input_window) != AMEDIA_OK || !input_window) {
      diagnostic = "MediaCodec failed to create its encoder input surface";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    input_window_ = input_window;

    const auto surface_result = create_encoder_egl_surface_locked(diagnostic);
    if (surface_result != DIGITOR_RESULT_OK) {
      cleanup_locked(false);
      return surface_result;
    }

    final_path_ = config.output_path;
    temp_path_ = final_path_ + ".digitor-partial";
    (void)::unlink(temp_path_.c_str());
    output_fd_ = ::open(temp_path_.c_str(), O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC,
                        0600);
    if (output_fd_ < 0) {
      diagnostic = "Android export destination is not writable";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    muxer_ = AMediaMuxer_new(output_fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!muxer_) {
      diagnostic = "failed to create Android MP4 muxer";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    if (AMediaCodec_start(codec_) != AMEDIA_OK) {
      diagnostic = "failed to start Android MediaCodec encoder";
      cleanup_locked(false);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    codec_started_ = true;
    opened_ = true;
    cancelled_ = false;
    eos_signaled_ = false;
    eos_observed_ = false;
    muxer_started_ = false;
    track_index_ = -1;
    qualification_ = {};
    qualification_.codec_opened = true;
    qualification_.input_surface_created = true;
    qualification_.ahardwarebuffer_or_surface_bound = true;
    qualification_.no_cpu_readback = true;
    qualification_.no_cpu_staging = true;

    capabilities = {};
    capabilities.interop = AndroidGpuInterop::gles_eglimage_surface;
    capabilities.available = true;
    capabilities.hardware_codec = true;
    capabilities.input_surface = true;
    capabilities.h264 = codec == ExportCodec::h264;
    capabilities.hevc = codec == ExportCodec::hevc;
    capabilities.ten_bit = false;
    capabilities.hdr_metadata = false;
    capabilities.mp4 = true;
    capabilities.max_width = static_cast<std::uint32_t>(config.profile.width);
    capabilities.max_height = static_cast<std::uint32_t>(config.profile.height);
    capabilities.api_level = api_level_;
    capabilities.codec_name = codec_name_;
    capabilities.device_identity =
        std::to_string(reinterpret_cast<std::uintptr_t>(frame_context_identity_));

    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

  DigitorResult submit(const AndroidHardwareEncodeFrameDescriptor& input,
                       std::string& diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    if (!opened_ || cancelled_ || !codec_started_ || !input.frame) {
      diagnostic = "Android MediaCodec export session is inactive";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    if (input.frame->backend() != DIGITOR_RENDERER_OPENGL_ES ||
        !input.frame->ready() || !input.frame->context_live() ||
        !input.frame->has_context_identity(frame_context_identity_)) {
      diagnostic = "Android encoder received an incompatible GLES frame";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    const auto previous_display = eglGetCurrentDisplay();
    const auto previous_context = eglGetCurrentContext();
    const auto previous_draw = eglGetCurrentSurface(EGL_DRAW);
    const auto previous_read = eglGetCurrentSurface(EGL_READ);

    if (eglMakeCurrent(display_, encoder_surface_, encoder_surface_, context_) !=
        EGL_TRUE) {
      diagnostic = "failed to bind MediaCodec EGL input surface";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    const auto srgb_was_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    auto result = renderer_->present_gpu_frame(input.frame);
    if (result == DIGITOR_RESULT_OK && presentation_time_) {
      if (presentation_time_(display_, encoder_surface_, input.pts_us * 1000LL) !=
          EGL_TRUE) {
        result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
    if (result == DIGITOR_RESULT_OK &&
        eglSwapBuffers(display_, encoder_surface_) != EGL_TRUE) {
      result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    if (!srgb_was_enabled) glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    restore_egl_locked(previous_display, previous_draw, previous_read,
                       previous_context);

    if (result != DIGITOR_RESULT_OK) {
      diagnostic = "processed GLES frame could not be submitted to MediaCodec";
      return result;
    }

    qualification_.gpu_frame_submitted = true;
    qualification_.acquire_sync_waited = true;
    qualification_.release_sync_published = true;
    qualification_.ahardwarebuffer_or_surface_bound = true;
    ++qualification_.submitted_frames;

    const auto drain_result = drain_codec_locked(false, diagnostic);
    if (drain_result != DIGITOR_RESULT_OK) return drain_result;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

  DigitorResult drain(std::string& diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    if (!opened_ || cancelled_) {
      diagnostic = "Android MediaCodec export session is inactive";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    if (!eos_signaled_) {
      if (!signal_eos_ || signal_eos_(codec_) != AMEDIA_OK) {
        diagnostic = "MediaCodec failed to signal end of input stream";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      eos_signaled_ = true;
    }
    return drain_codec_locked(true, diagnostic);
  }

  DigitorResult finalize(std::string& diagnostic) noexcept {
    std::scoped_lock lock(mutex_);
    if (!opened_ || cancelled_) {
      diagnostic = "Android MediaCodec export session is inactive";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    if (!eos_observed_) {
      diagnostic = "MediaCodec output was not fully drained before finalization";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    if (!muxer_ || !muxer_started_ || !qualification_.bitstream_produced) {
      diagnostic = "MediaCodec produced no muxable MP4 video stream";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    if (AMediaMuxer_stop(muxer_) != AMEDIA_OK) {
      diagnostic = "Android MP4 muxer failed to finalize";
      qualification_.diagnostic = diagnostic;
      cleanup_locked(true);
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    muxer_started_ = false;
    AMediaMuxer_delete(muxer_);
    muxer_ = nullptr;

    if (codec_started_) {
      (void)AMediaCodec_stop(codec_);
      codec_started_ = false;
    }
    destroy_encoder_surface_locked();
    if (codec_) {
      (void)AMediaCodec_delete(codec_);
      codec_ = nullptr;
    }
    if (output_fd_ >= 0) {
      ::close(output_fd_);
      output_fd_ = -1;
    }

    if (std::rename(temp_path_.c_str(), final_path_.c_str()) != 0) {
      diagnostic = "Android export could not atomically publish the MP4 file";
      qualification_.diagnostic = diagnostic;
      (void)::unlink(temp_path_.c_str());
      opened_ = false;
      cancelled_ = true;
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    qualification_.mp4_finalized = true;
    qualification_.diagnostic.clear();
    opened_ = false;
    finalized_ = true;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

  void cancel() noexcept {
    std::scoped_lock lock(mutex_);
    if (finalized_) return;
    cancelled_ = true;
    cleanup_locked(true);
  }

  AndroidHardwareEncodeQualification qualification() const {
    std::scoped_lock lock(mutex_);
    return qualification_;
  }

 private:
  std::string codec_component_name_locked() noexcept {
    const auto get_name = get_codec_name_fn();
    const auto release_name = release_codec_name_fn();
    if (!get_name || !release_name || !codec_) return {};
    char* raw = nullptr;
    if (get_name(codec_, &raw) != AMEDIA_OK || !raw) return {};
    std::string out(raw);
    release_name(codec_, raw);
    return out;
  }

  DigitorResult create_encoder_egl_surface_locked(
      std::string& diagnostic) noexcept {
    EGLint config_id = 0;
    if (eglQueryContext(display_, context_, EGL_CONFIG_ID, &config_id) != EGL_TRUE) {
      diagnostic = "failed to query GLES EGLConfig for MediaCodec export";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    const EGLint choose_attributes[] = {EGL_CONFIG_ID, config_id, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (eglChooseConfig(display_, choose_attributes, &config, 1, &count) != EGL_TRUE ||
        count != 1 || !config) {
      diagnostic = "failed to resolve GLES EGLConfig for MediaCodec export";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    EGLint surface_type = 0;
    if (eglGetConfigAttrib(display_, config, EGL_SURFACE_TYPE, &surface_type) != EGL_TRUE ||
        (surface_type & EGL_WINDOW_BIT) == 0) {
      diagnostic = "selected GLES EGLConfig cannot render into MediaCodec surface";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    const char* egl_extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (!extension_present(egl_extensions, "EGL_KHR_gl_colorspace") ||
        !extension_present(egl_extensions, "EGL_ANDROID_presentation_time")) {
      diagnostic =
          "Android GLES encoder requires EGL colorspace and presentation-time extensions";
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    presentation_time_ = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    if (!presentation_time_) {
      diagnostic = "EGL_ANDROID_presentation_time entry point is unavailable";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    const EGLint surface_attributes[] = {
        EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SRGB_KHR, EGL_NONE};
    encoder_surface_ = eglCreateWindowSurface(
        display_, config, input_window_, surface_attributes);
    if (encoder_surface_ == EGL_NO_SURFACE) {
      diagnostic = "failed to create EGL surface for MediaCodec input";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

  DigitorResult start_muxer_locked(std::string& diagnostic) noexcept {
    if (muxer_started_) return DIGITOR_RESULT_OK;
    AMediaFormat* output_format = AMediaCodec_getOutputFormat(codec_);
    if (!output_format) {
      diagnostic = "MediaCodec output format is unavailable";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    track_index_ = AMediaMuxer_addTrack(muxer_, output_format);
    AMediaFormat_delete(output_format);
    if (track_index_ < 0) {
      diagnostic = "Android MP4 muxer rejected the encoded video track";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    if (AMediaMuxer_start(muxer_) != AMEDIA_OK) {
      diagnostic = "Android MP4 muxer failed to start";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    muxer_started_ = true;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult drain_codec_locked(bool wait_for_eos,
                                   std::string& diagnostic) noexcept {
    if (!codec_ || !codec_started_ || !muxer_) {
      diagnostic = "Android MediaCodec drain called before encoder start";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    constexpr int kMaxEosPolls = 1000;
    int eos_polls = 0;
    while (true) {
      AMediaCodecBufferInfo info{};
      const auto index = AMediaCodec_dequeueOutputBuffer(
          codec_, &info, wait_for_eos ? 10000 : 0);
      if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        if (!wait_for_eos) return DIGITOR_RESULT_OK;
        if (++eos_polls >= kMaxEosPolls) {
          diagnostic = "timed out draining Android MediaCodec encoder";
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }
        continue;
      }
      if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        if (muxer_started_) {
          diagnostic = "MediaCodec output format changed after muxer start";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
        const auto result = start_muxer_locked(diagnostic);
        if (result != DIGITOR_RESULT_OK) return result;
        continue;
      }
      if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
      if (index < 0) {
        diagnostic = "unexpected MediaCodec output dequeue result";
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }

      const auto buffer_index = static_cast<std::size_t>(index);
      size_t reported_buffer_size = 0;
      std::uint8_t* buffer =
          AMediaCodec_getOutputBuffer(codec_, buffer_index, &reported_buffer_size);
      const bool codec_config =
          (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
      const bool eos =
          (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;

      if (info.size > 0 && !codec_config) {
        if (!muxer_started_) {
          const auto result = start_muxer_locked(diagnostic);
          if (result != DIGITOR_RESULT_OK) {
            (void)AMediaCodec_releaseOutputBuffer(codec_, buffer_index, false);
            return result;
          }
        }
        if (!buffer) {
          (void)AMediaCodec_releaseOutputBuffer(codec_, buffer_index, false);
          diagnostic = "MediaCodec returned a null encoded output buffer";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }

        const auto safe_offset = info.offset > 0
                                     ? static_cast<std::size_t>(info.offset)
                                     : std::size_t{0};
        const auto sample_size = static_cast<std::size_t>(info.size);
        const std::uint8_t* sample = buffer;
        if (reported_buffer_size >= safe_offset + sample_size) sample += safe_offset;
        AMediaCodecBufferInfo mux_info = info;
        mux_info.offset = 0;
        if (AMediaMuxer_writeSampleData(
                muxer_, static_cast<std::size_t>(track_index_), sample,
                &mux_info) != AMEDIA_OK) {
          (void)AMediaCodec_releaseOutputBuffer(codec_, buffer_index, false);
          diagnostic = "Android MP4 muxer failed to write encoded video sample";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
        qualification_.bitstream_produced = true;
        ++qualification_.encoded_frames;
      }

      (void)AMediaCodec_releaseOutputBuffer(codec_, buffer_index, false);
      if (eos) {
        eos_observed_ = true;
        diagnostic.clear();
        return DIGITOR_RESULT_OK;
      }
      if (!wait_for_eos) continue;
    }
  }

  void restore_egl_locked(EGLDisplay display, EGLSurface draw, EGLSurface read,
                          EGLContext context) noexcept {
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
      (void)eglMakeCurrent(display, draw, read, context);
    } else {
      (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
    }
  }

  void destroy_encoder_surface_locked() noexcept {
    if (encoder_surface_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY) {
      (void)eglDestroySurface(display_, encoder_surface_);
      encoder_surface_ = EGL_NO_SURFACE;
    }
    if (input_window_) {
      ANativeWindow_release(input_window_);
      input_window_ = nullptr;
    }
  }

  void cleanup_locked(bool remove_partial) noexcept {
    if (muxer_) {
      if (muxer_started_) (void)AMediaMuxer_stop(muxer_);
      AMediaMuxer_delete(muxer_);
      muxer_ = nullptr;
      muxer_started_ = false;
    }
    if (codec_started_ && codec_) {
      (void)AMediaCodec_stop(codec_);
      codec_started_ = false;
    }
    destroy_encoder_surface_locked();
    if (codec_) {
      (void)AMediaCodec_delete(codec_);
      codec_ = nullptr;
    }
    if (output_fd_ >= 0) {
      ::close(output_fd_);
      output_fd_ = -1;
    }
    if (remove_partial && !temp_path_.empty()) (void)::unlink(temp_path_.c_str());
    opened_ = false;
  }

  mutable std::mutex mutex_;
  EGLDisplay display_{EGL_NO_DISPLAY};
  EGLContext context_{EGL_NO_CONTEXT};
  IRenderBackend* renderer_{};
  const void* frame_context_identity_{};
  EGLSurface encoder_surface_{EGL_NO_SURFACE};
  PFNEGLPRESENTATIONTIMEANDROIDPROC presentation_time_{};
  AMediaCodec* codec_{};
  AMediaMuxer* muxer_{};
  ANativeWindow* input_window_{};
  SignalEndOfInputStreamFn signal_eos_{};
  int output_fd_{-1};
  int api_level_{};
  std::int32_t track_index_{-1};
  std::string codec_name_;
  std::string final_path_;
  std::string temp_path_;
  AndroidHardwareEncodeQualification qualification_;
  bool opened_{};
  bool codec_started_{};
  bool muxer_started_{};
  bool eos_signaled_{};
  bool eos_observed_{};
  bool cancelled_{};
  bool finalized_{};
};

inline AndroidHardwareEncoderHost make_android_gles_mediacodec_host(
    EGLDisplay display, EGLContext context, IRenderBackend* renderer,
    const void* frame_context_identity) {
  auto encoder = std::make_shared<AndroidGlesMediaCodecEncoder>(
      display, context, renderer, frame_context_identity);
  AndroidHardwareEncoderHost host{};
  host.open = [encoder](const HardwareEncodeConfig& config,
                        const ExportRenderSnapshot& snapshot,
                        AndroidHardwareEncodeCapabilities& capabilities,
                        std::string& diagnostic) {
    return encoder->open(config, snapshot, capabilities, diagnostic);
  };
  host.submit = [encoder](const AndroidHardwareEncodeFrameDescriptor& input,
                          std::string& diagnostic) {
    return encoder->submit(input, diagnostic);
  };
  host.drain = [encoder](std::string& diagnostic) {
    return encoder->drain(diagnostic);
  };
  host.finalize_mp4_atomic = [encoder](std::string& diagnostic) {
    return encoder->finalize(diagnostic);
  };
  host.cancel = [encoder]() { encoder->cancel(); };
  host.qualification = [encoder]() { return encoder->qualification(); };
  return host;
}

inline ProductionEncoderFactory make_android_gles_mediacodec_encoder_factory(
    EGLDisplay display, EGLContext context, IRenderBackend* renderer,
    const void* frame_context_identity) {
  return [display, context, renderer, frame_context_identity](
             std::shared_ptr<const ExportRenderSnapshot> snapshot) mutable {
    auto host = make_android_gles_mediacodec_host(
        display, context, renderer, frame_context_identity);
    return create_production_encoder(
        ProductionPlatform::android, std::move(snapshot), {}, std::move(host),
        {}, {});
  };
}

}  // namespace android_gles_mediacodec_detail
}  // namespace digitor

#endif  // defined(__ANDROID__)
