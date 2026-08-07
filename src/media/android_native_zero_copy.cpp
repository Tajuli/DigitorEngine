#include "digitor/android_native_zero_copy.hpp"

#include <mutex>
#include <new>
#include <utility>

#if defined(__ANDROID__)
#include <android/hardware_buffer.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#endif

namespace digitor {

struct AndroidNativeZeroCopyBindings::Impl {
  AndroidNativeInteropConfig config;
  AndroidVulkanRgba16fDispatch vulkan_dispatch;
  AndroidGlesRgba16fDispatch gles_dispatch;
  AndroidMediaCodecSurfaceSubmit encoder_submit;
  AndroidVulkanImport native_vulkan_import;
  AndroidGlesImport native_gles_import;
  mutable std::mutex mutex;
  AndroidNativeInteropTelemetry telemetry;
#if defined(__ANDROID__)
  PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb_properties{};
  PFN_vkGetMemoryAndroidHardwareBufferANDROID get_memory_ahb{};
  PFN_vkImportSemaphoreFdKHR import_semaphore_fd{};
  PFNEGLCREATEIMAGEKHRPROC egl_create_image{};
  PFNEGLDESTROYIMAGEKHRPROC egl_destroy_image{};
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC gl_image_target{};
#endif
};

AndroidNativeZeroCopyBindings::AndroidNativeZeroCopyBindings(
    AndroidNativeInteropConfig c,
    AndroidVulkanRgba16fDispatch vd,
    AndroidGlesRgba16fDispatch gd,
    AndroidMediaCodecSurfaceSubmit es,
    AndroidVulkanImport vi,
    AndroidGlesImport gi)
    : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
  impl_->vulkan_dispatch = std::move(vd);
  impl_->gles_dispatch = std::move(gd);
  impl_->encoder_submit = std::move(es);
  impl_->native_vulkan_import = std::move(vi);
  impl_->native_gles_import = std::move(gi);
}

AndroidNativeZeroCopyBindings::~AndroidNativeZeroCopyBindings() = default;

DigitorResult AndroidNativeZeroCopyBindings::initialize() noexcept {
#if !defined(__ANDROID__)
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (i.config.require_rgba16f && !i.vulkan_dispatch && !i.gles_dispatch)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (!i.encoder_submit) return DIGITOR_RESULT_INVALID_ARGUMENT;

  if (i.config.vulkan.device) {
    auto device = static_cast<VkDevice>(i.config.vulkan.device);
    i.get_ahb_properties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    i.get_memory_ahb = reinterpret_cast<PFN_vkGetMemoryAndroidHardwareBufferANDROID>(
        vkGetDeviceProcAddr(device, "vkGetMemoryAndroidHardwareBufferANDROID"));
    i.import_semaphore_fd = reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR"));
    if (i.config.require_vulkan_external_memory &&
        (!i.get_ahb_properties || !i.get_memory_ahb || !i.native_vulkan_import)) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.failures;
      i.telemetry.diagnostic =
          "Vulkan AHardwareBuffer import requires extension entry points and renderer-owned VkImage binding";
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    if (i.config.require_sync_fd && !i.import_semaphore_fd) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.failures;
      i.telemetry.diagnostic = "VK_KHR_external_semaphore_fd unavailable";
      return DIGITOR_RESULT_UNSUPPORTED;
    }
  }

  if (i.config.allow_gles_external_image && i.config.egl.display && !i.native_gles_import) {
    i.egl_create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    i.egl_destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    i.gl_image_target = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!i.egl_create_image || !i.egl_destroy_image || !i.gl_image_target) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.failures;
      i.telemetry.diagnostic = "EGLImage external-texture entry points unavailable";
      return DIGITOR_RESULT_UNSUPPORTED;
    }
  }

  std::scoped_lock lock(i.mutex);
  i.telemetry.diagnostic = "Android native zero-copy interop initialized";
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidNativeZeroCopyBindings::import_vulkan(
    const AndroidHardwareBufferFrame& frame,
    AndroidImportedImage& out) noexcept {
  out = {};
#if !defined(__ANDROID__)
  (void)frame;
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!frame.hardware_buffer || !frame.width || !frame.height ||
      !i.config.vulkan.device || !i.config.vulkan.physical_device ||
      !i.get_ahb_properties || !i.native_vulkan_import)
    return DIGITOR_RESULT_INVALID_ARGUMENT;

  AHardwareBuffer_Desc desc{};
  AHardwareBuffer_describe(static_cast<AHardwareBuffer*>(frame.hardware_buffer), &desc);
  if (desc.width != frame.width || desc.height != frame.height) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "AHardwareBuffer dimensions mismatch";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if ((desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) == 0) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "AHardwareBuffer is not GPU sampleable";
    return DIGITOR_RESULT_UNSUPPORTED;
  }

  VkAndroidHardwareBufferFormatPropertiesANDROID format_props{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
  VkAndroidHardwareBufferPropertiesANDROID props{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
  props.pNext = &format_props;
  auto device = static_cast<VkDevice>(i.config.vulkan.device);
  auto ahb = static_cast<AHardwareBuffer*>(frame.hardware_buffer);
  if (i.get_ahb_properties(device, ahb, &props) != VK_SUCCESS ||
      props.allocationSize == 0 || props.memoryTypeBits == 0) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "Unable to query AHardwareBuffer Vulkan properties";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  // The renderer owns VkDevice/VkQueue lifetime and therefore owns creation of
  // VkImage, VkDeviceMemory, VkImageView, YCbCr conversion and sync import. Do
  // not expose AHardwareBuffer/externalFormat integers as fake Vulkan handles.
  const auto result = i.native_vulkan_import(frame, out);
  if (result != DIGITOR_RESULT_OK) {
    out = {};
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "renderer-owned AHardwareBuffer Vulkan import failed";
    return result;
  }
  if (out.backend != AndroidZeroCopyBackend::vulkan || !out.image || !out.image_view ||
      !out.lifetime || out.width != frame.width || out.height != frame.height ||
      out.format != frame.format || out.timestamp_us != frame.timestamp_us ||
      out.frame_identity != frame.frame_identity) {
    out = {};
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "renderer-owned Vulkan import violated native image contract";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  if (i.config.require_sync_fd && frame.acquire_fence_fd >= 0 && !out.completion_sync) {
    out = {};
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "Vulkan import did not preserve acquire synchronization";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }

  std::scoped_lock lock(i.mutex);
  ++i.telemetry.vulkan_imports;
  if (frame.acquire_fence_fd >= 0) ++i.telemetry.sync_fd_imports;
  i.telemetry.diagnostic = "AHardwareBuffer imported as renderer-owned Vulkan image";
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidNativeZeroCopyBindings::import_gles(
    const AndroidHardwareBufferFrame& frame,
    AndroidImportedImage& out) noexcept {
  out = {};
#if !defined(__ANDROID__)
  (void)frame;
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!i.config.allow_gles_external_image || !i.config.egl.display || !frame.hardware_buffer)
    return DIGITOR_RESULT_UNSUPPORTED;

  if (i.native_gles_import) {
    const auto result = i.native_gles_import(frame, out);
    if (result != DIGITOR_RESULT_OK) return result;
    if (out.backend != AndroidZeroCopyBackend::opengl_es || !out.image || !out.image_view ||
        !out.lifetime || out.width != frame.width || out.height != frame.height ||
        out.timestamp_us != frame.timestamp_us || out.frame_identity != frame.frame_identity) {
      out = {};
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.egl_imports;
    i.telemetry.diagnostic = "AHardwareBuffer imported by renderer-owned GLES binding";
    return DIGITOR_RESULT_OK;
  }

  if (!i.egl_create_image || !i.gl_image_target) return DIGITOR_RESULT_UNSUPPORTED;
  const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  auto display = static_cast<EGLDisplay>(i.config.egl.display);
  auto image = i.egl_create_image(display, EGL_NO_CONTEXT,
      EGL_NATIVE_BUFFER_ANDROID,
      static_cast<EGLClientBuffer>(frame.hardware_buffer), attrs);
  if (image == EGL_NO_IMAGE_KHR) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "eglCreateImageKHR failed for AHardwareBuffer";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  GLuint texture{};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
  i.gl_image_target(GL_TEXTURE_EXTERNAL_OES, image);
  const GLenum error = glGetError();
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  if (error != GL_NO_ERROR) {
    glDeleteTextures(1, &texture);
    i.egl_destroy_image(display, image);
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "glEGLImageTargetTexture2DOES failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  struct EglLifetime {
    EGLDisplay display{};
    EGLImageKHR image{};
    GLuint texture{};
    PFNEGLDESTROYIMAGEKHRPROC destroy{};
    ~EglLifetime() {
      if (texture) glDeleteTextures(1, &texture);
      if (image != EGL_NO_IMAGE_KHR && destroy) destroy(display, image);
    }
  };
  auto holder = std::make_shared<EglLifetime>();
  holder->display = display;
  holder->image = image;
  holder->texture = texture;
  holder->destroy = i.egl_destroy_image;

  out.backend = AndroidZeroCopyBackend::opengl_es;
  out.image = image;
  out.image_view = reinterpret_cast<void*>(static_cast<std::uintptr_t>(texture));
  out.width = frame.width;
  out.height = frame.height;
  out.format = frame.format;
  out.timestamp_us = frame.timestamp_us;
  out.frame_identity = frame.frame_identity;
  out.lifetime = std::move(holder);

  std::scoped_lock lock(i.mutex);
  ++i.telemetry.egl_imports;
  i.telemetry.diagnostic = "AHardwareBuffer imported as EGLImage external texture";
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidNativeZeroCopyBindings::convert(
    const AndroidImportedImage& image,
    ProcessedGpuFramePtr& out) noexcept {
  out.reset();
  auto& i = *impl_;
  DigitorResult result = DIGITOR_RESULT_UNSUPPORTED;
  if (image.backend == AndroidZeroCopyBackend::vulkan && i.vulkan_dispatch)
    result = i.vulkan_dispatch(image, out);
  else if (image.backend == AndroidZeroCopyBackend::opengl_es && i.gles_dispatch)
    result = i.gles_dispatch(image, out);
  if (result != DIGITOR_RESULT_OK || !out || !out->ready() ||
      out->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
      out->metadata().timestamp != image.timestamp_us) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.failures;
    i.telemetry.diagnostic = "Android GPU conversion did not produce matching RGBA16F frame";
    out.reset();
    return result == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : result;
  }
  std::scoped_lock lock(i.mutex);
  ++i.telemetry.rgba16f_dispatches;
  return DIGITOR_RESULT_OK;
}

DigitorResult AndroidNativeZeroCopyBindings::submit_encoder(
    const ProcessedGpuFramePtr& frame) noexcept {
  if (!frame || !frame->ready() || frame->backend() == DIGITOR_RENDERER_CPU ||
      frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto result = impl_->encoder_submit(frame);
  std::scoped_lock lock(impl_->mutex);
  if (result == DIGITOR_RESULT_OK) ++impl_->telemetry.encoder_submissions;
  else ++impl_->telemetry.failures;
  return result;
}

AndroidZeroCopyBinding AndroidNativeZeroCopyBindings::binding(
    AndroidMediaCodecAcquire acquire,
    AndroidGpuFrameConsumer preview_consumer) {
  auto keep = impl_;
  AndroidZeroCopyBinding b{};
  b.acquire_decoder_frame = std::move(acquire);
  b.import_vulkan = [keep](const AndroidHardwareBufferFrame& f,
                           AndroidImportedImage& o) noexcept {
    AndroidNativeZeroCopyBindings x(keep->config, keep->vulkan_dispatch,
                                    keep->gles_dispatch, keep->encoder_submit,
                                    keep->native_vulkan_import, keep->native_gles_import);
    x.impl_ = keep;
    return x.import_vulkan(f, o);
  };
  b.import_gles = [keep](const AndroidHardwareBufferFrame& f,
                         AndroidImportedImage& o) noexcept {
    AndroidNativeZeroCopyBindings x(keep->config, keep->vulkan_dispatch,
                                    keep->gles_dispatch, keep->encoder_submit,
                                    keep->native_vulkan_import, keep->native_gles_import);
    x.impl_ = keep;
    return x.import_gles(f, o);
  };
  b.convert_to_rgba16f = [keep](const AndroidImportedImage& f,
                                ProcessedGpuFramePtr& o) noexcept {
    AndroidNativeZeroCopyBindings x(keep->config, keep->vulkan_dispatch,
                                    keep->gles_dispatch, keep->encoder_submit,
                                    keep->native_vulkan_import, keep->native_gles_import);
    x.impl_ = keep;
    return x.convert(f, o);
  };
  b.preview_consumer = std::move(preview_consumer);
  b.encoder_consumer = [keep](const ProcessedGpuFramePtr& f) noexcept {
    AndroidNativeZeroCopyBindings x(keep->config, keep->vulkan_dispatch,
                                    keep->gles_dispatch, keep->encoder_submit,
                                    keep->native_vulkan_import, keep->native_gles_import);
    x.impl_ = keep;
    return x.submit_encoder(f);
  };
  return b;
}

AndroidNativeInteropTelemetry AndroidNativeZeroCopyBindings::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

bool AndroidNativeZeroCopyBindings::gpu_only() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry.cpu_copies == 0;
}

}  // namespace digitor
