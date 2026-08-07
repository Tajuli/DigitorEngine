#include "digitor/android_zero_copy_concrete_bindings.hpp"

#include <mutex>
#include <new>
#include <utility>

#ifdef __ANDROID__
#include <unistd.h>
#endif

namespace digitor {
namespace {

[[nodiscard]] bool valid_format(const AndroidHardwareBufferFrame& f) noexcept {
  if (!f.hardware_buffer || !f.width || !f.height || f.timestamp_us < 0 ||
      f.frame_identity == 0 || !f.decoder_lifetime) return false;
  if (f.format == AndroidYuvFormat::p010) return f.bit_depth == 10;
  return f.bit_depth == 8;
}

[[nodiscard]] bool valid_import(const AndroidHardwareBufferFrame& in,
                                const AndroidImportedImage& out,
                                AndroidZeroCopyBackend backend) noexcept {
  return out.backend == backend && out.image && out.image_view && out.lifetime &&
         out.width == in.width && out.height == in.height &&
         out.format == in.format && out.timestamp_us == in.timestamp_us &&
         out.frame_identity == in.frame_identity;
}

}  // namespace

struct AndroidMediaCodecSurfaceDecoder::Impl {
  AndroidMediaCodecDecoderConfig config;
  AndroidCodecSurfaceAcquire acquire_surface;
  bool initialized{};
};

AndroidMediaCodecSurfaceDecoder::AndroidMediaCodecSurfaceDecoder(
    AndroidMediaCodecDecoderConfig c, AndroidCodecSurfaceAcquire a)
    : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
  impl_->acquire_surface = std::move(a);
}
AndroidMediaCodecSurfaceDecoder::~AndroidMediaCodecSurfaceDecoder() = default;

DigitorResult AndroidMediaCodecSurfaceDecoder::initialize() noexcept {
  if (!impl_->config.width || !impl_->config.height || impl_->config.mime.empty() ||
      !impl_->config.output_surface || !impl_->acquire_surface)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (impl_->config.bit_depth != 8 && impl_->config.bit_depth != 10)
    return DIGITOR_RESULT_UNSUPPORTED;
#ifndef __ANDROID__
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  impl_->initialized = true;
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidMediaCodecSurfaceDecoder::acquire(
    std::int64_t timestamp_us, AndroidHardwareBufferFrame& out) noexcept {
  out = {};
  if (!impl_->initialized || timestamp_us < 0) return DIGITOR_RESULT_NOT_INITIALIZED;
  void* buffer{};
  int fence_fd{-1};
  std::shared_ptr<void> lifetime;
  const auto r = impl_->acquire_surface(timestamp_us, buffer, fence_fd, lifetime);
  if (r != DIGITOR_RESULT_OK) return r;
  out.hardware_buffer = buffer;
  out.acquire_fence_fd = fence_fd;
  out.width = impl_->config.width;
  out.height = impl_->config.height;
  out.bit_depth = impl_->config.bit_depth;
  out.format = impl_->config.bit_depth == 10 ? AndroidYuvFormat::p010
                                              : AndroidYuvFormat::implementation_defined;
  out.timestamp_us = timestamp_us;
  out.frame_identity = static_cast<std::uint64_t>(timestamp_us) + 1u;
  out.decoder_lifetime = std::move(lifetime);
  if (!valid_format(out)) {
#ifdef __ANDROID__
    if (out.acquire_fence_fd >= 0) ::close(out.acquire_fence_fd);
#endif
    out = {};
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  return DIGITOR_RESULT_OK;
}

AndroidMediaCodecAcquire AndroidMediaCodecSurfaceDecoder::callback() {
  auto keep = impl_;
  return [keep](std::int64_t t, AndroidHardwareBufferFrame& f) noexcept {
    AndroidMediaCodecSurfaceDecoder d(keep->config, keep->acquire_surface);
    d.impl_ = keep;
    return d.acquire(t, f);
  };
}

struct AndroidVulkanHardwareBufferImporter::Impl {
  AndroidVulkanExternalImportConfig config;
  bool initialized{};
};

AndroidVulkanHardwareBufferImporter::AndroidVulkanHardwareBufferImporter(
    AndroidVulkanExternalImportConfig c) : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
}
AndroidVulkanHardwareBufferImporter::~AndroidVulkanHardwareBufferImporter() = default;

DigitorResult AndroidVulkanHardwareBufferImporter::initialize() noexcept {
  if (!impl_->config.instance || !impl_->config.physical_device || !impl_->config.device ||
      !impl_->config.queue || !impl_->config.import_ahardware_buffer)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
#ifndef __ANDROID__
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  impl_->initialized = true;
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidVulkanHardwareBufferImporter::import(
    const AndroidHardwareBufferFrame& in, AndroidImportedImage& out) noexcept {
  out = {};
  if (!impl_->initialized) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (!valid_format(in)) return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto r = impl_->config.import_ahardware_buffer(in, out);
  if (r != DIGITOR_RESULT_OK) {
    out = {};
    return r;
  }
  if (!valid_import(in, out, AndroidZeroCopyBackend::vulkan)) {
    out = {};
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

AndroidVulkanImport AndroidVulkanHardwareBufferImporter::callback() {
  auto keep = impl_;
  return [keep](const AndroidHardwareBufferFrame& f, AndroidImportedImage& i) noexcept {
    AndroidVulkanHardwareBufferImporter v(keep->config);
    v.impl_ = keep;
    return v.import(f, i);
  };
}

struct AndroidGlesHardwareBufferImporter::Impl {
  AndroidGlesExternalImageConfig config;
  bool initialized{};
};

AndroidGlesHardwareBufferImporter::AndroidGlesHardwareBufferImporter(
    AndroidGlesExternalImageConfig c) : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
}
AndroidGlesHardwareBufferImporter::~AndroidGlesHardwareBufferImporter() = default;

DigitorResult AndroidGlesHardwareBufferImporter::initialize() noexcept {
  if (!impl_->config.egl_display || !impl_->config.egl_context ||
      !impl_->config.import_ahardware_buffer)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
#ifndef __ANDROID__
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  impl_->initialized = true;
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidGlesHardwareBufferImporter::import(
    const AndroidHardwareBufferFrame& in, AndroidImportedImage& out) noexcept {
  out = {};
  if (!impl_->initialized) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (!valid_format(in)) return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto r = impl_->config.import_ahardware_buffer(in, out);
  if (r != DIGITOR_RESULT_OK) {
    out = {};
    return r;
  }
  if (!valid_import(in, out, AndroidZeroCopyBackend::opengl_es)) {
    out = {};
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

AndroidGlesImport AndroidGlesHardwareBufferImporter::callback() {
  auto keep = impl_;
  return [keep](const AndroidHardwareBufferFrame& f, AndroidImportedImage& i) noexcept {
    AndroidGlesHardwareBufferImporter g(keep->config);
    g.impl_ = keep;
    return g.import(f, i);
  };
}

AndroidGpuYuvConverter::AndroidGpuYuvConverter(AndroidImportedYuvDispatch d)
    : dispatch_(std::move(d)) {}

DigitorResult AndroidGpuYuvConverter::convert(
    const AndroidImportedImage& in, ProcessedGpuFramePtr& out) noexcept {
  out.reset();
  if (!dispatch_ || !in.image || !in.image_view || !in.width || !in.height)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto r = dispatch_(in, AndroidColorMatrix::bt709,
                           AndroidColorRange::limited,
                           in.format == AndroidYuvFormat::p010 ? 10u : 8u, out);
  if (r != DIGITOR_RESULT_OK) return r;
  if (!out || !out->ready() || out->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
      out->metadata().timestamp != in.timestamp_us || out->identity() != in.frame_identity)
    return DIGITOR_RESULT_INTERNAL_ERROR;
  return DIGITOR_RESULT_OK;
}

AndroidYuvToRgba16f AndroidGpuYuvConverter::callback() {
  return [dispatch = dispatch_](const AndroidImportedImage& in,
                                ProcessedGpuFramePtr& out) noexcept {
    AndroidGpuYuvConverter c(dispatch);
    return c.convert(in, out);
  };
}

struct AndroidMediaCodecHardwareEncoder::Impl {
  AndroidHardwareEncoderConfig config;
  AndroidEncoderSubmit submit;
  bool initialized{};
  std::mutex mutex;
  std::int64_t last_timestamp{-1};
};

AndroidMediaCodecHardwareEncoder::AndroidMediaCodecHardwareEncoder(
    AndroidHardwareEncoderConfig c, AndroidEncoderSubmit s)
    : impl_(std::make_shared<Impl>()) {
  impl_->config = std::move(c);
  impl_->submit = std::move(s);
}
AndroidMediaCodecHardwareEncoder::~AndroidMediaCodecHardwareEncoder() = default;

DigitorResult AndroidMediaCodecHardwareEncoder::initialize() noexcept {
  if (impl_->config.mime.empty() || !impl_->config.width || !impl_->config.height ||
      !impl_->config.fps || !impl_->config.bitrate || !impl_->config.input_surface ||
      !impl_->submit)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (impl_->config.bit_depth != 8 && impl_->config.bit_depth != 10)
    return DIGITOR_RESULT_UNSUPPORTED;
#ifndef __ANDROID__
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  impl_->initialized = true;
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidMediaCodecHardwareEncoder::consume(
    const ProcessedGpuFramePtr& frame) noexcept {
  if (!impl_->initialized || !frame || !frame->ready()) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (frame->backend() == DIGITOR_RENDERER_CPU ||
      frame->metadata().format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)
    return DIGITOR_RESULT_UNSUPPORTED;
  std::scoped_lock lock(impl_->mutex);
  if (impl_->last_timestamp >= 0 && frame->metadata().timestamp < impl_->last_timestamp)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto r = impl_->submit(frame, impl_->config.input_surface);
  if (r == DIGITOR_RESULT_OK) impl_->last_timestamp = frame->metadata().timestamp;
  return r;
}

AndroidGpuFrameConsumer AndroidMediaCodecHardwareEncoder::callback() {
  auto keep = impl_;
  return [keep](const ProcessedGpuFramePtr& f) noexcept {
    AndroidMediaCodecHardwareEncoder e(keep->config, keep->submit);
    e.impl_ = keep;
    return e.consume(f);
  };
}

DigitorResult AndroidMediaCodecHardwareEncoder::flush() noexcept {
  return impl_->initialized ? DIGITOR_RESULT_OK : DIGITOR_RESULT_NOT_INITIALIZED;
}

struct AndroidConcreteZeroCopyPipeline::Impl {
  std::unique_ptr<AndroidMediaCodecSurfaceDecoder> decoder;
  std::unique_ptr<AndroidVulkanHardwareBufferImporter> vulkan;
  std::unique_ptr<AndroidGlesHardwareBufferImporter> gles;
  std::unique_ptr<AndroidGpuYuvConverter> converter;
  std::unique_ptr<AndroidMediaCodecHardwareEncoder> encoder;
  std::unique_ptr<AndroidZeroCopyPipeline> pipeline;
};

AndroidConcreteZeroCopyPipeline::AndroidConcreteZeroCopyPipeline(
    AndroidZeroCopyConfig c, AndroidConcretePipelineBinding b)
    : impl_(std::make_unique<Impl>()) {
  impl_->decoder = std::make_unique<AndroidMediaCodecSurfaceDecoder>(
      b.decoder, b.decoder_surface_acquire);
  impl_->vulkan = std::make_unique<AndroidVulkanHardwareBufferImporter>(std::move(b.vulkan));
  impl_->gles = std::make_unique<AndroidGlesHardwareBufferImporter>(std::move(b.gles));
  impl_->converter = std::make_unique<AndroidGpuYuvConverter>(b.yuv_dispatch);
  impl_->encoder = std::make_unique<AndroidMediaCodecHardwareEncoder>(b.encoder, b.encoder_submit);
  AndroidZeroCopyBinding binding{};
  binding.acquire_decoder_frame = impl_->decoder->callback();
  binding.import_vulkan = impl_->vulkan->callback();
  binding.import_gles = impl_->gles->callback();
  binding.convert_to_rgba16f = impl_->converter->callback();
  binding.preview_consumer = std::move(b.preview_consumer);
  binding.encoder_consumer = impl_->encoder->callback();
  impl_->pipeline = std::make_unique<AndroidZeroCopyPipeline>(std::move(c), std::move(binding));
}
AndroidConcreteZeroCopyPipeline::~AndroidConcreteZeroCopyPipeline() = default;

DigitorResult AndroidConcreteZeroCopyPipeline::initialize() noexcept {
  auto r = impl_->decoder->initialize();
  if (r != DIGITOR_RESULT_OK) return r;
  r = impl_->vulkan->initialize();
  if (r != DIGITOR_RESULT_OK) {
    const auto g = impl_->gles->initialize();
    if (g != DIGITOR_RESULT_OK) return r;
  }
  r = impl_->encoder->initialize();
  if (r != DIGITOR_RESULT_OK) return r;
  return impl_->pipeline->initialize();
}

DigitorResult AndroidConcreteZeroCopyPipeline::preview(std::int64_t t) noexcept {
  return impl_->pipeline->preview(t);
}
DigitorResult AndroidConcreteZeroCopyPipeline::export_frame(std::int64_t t) noexcept {
  return impl_->pipeline->export_frame(t);
}
DigitorResult AndroidConcreteZeroCopyPipeline::preview_and_export(std::int64_t t) noexcept {
  return impl_->pipeline->preview_and_export(t);
}
AndroidZeroCopyTelemetry AndroidConcreteZeroCopyPipeline::telemetry() const {
  return impl_->pipeline->telemetry();
}

}  // namespace digitor
