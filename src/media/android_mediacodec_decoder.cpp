#include "digitor/android_mediacodec_decoder.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>

#if defined(__ANDROID__)
#include <android/api-level.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <unistd.h>
#endif

namespace digitor {
namespace {

bool accepted_mime(const char* mime) noexcept {
  return mime && (!std::strcmp(mime, "video/avc") ||
                  !std::strcmp(mime, "video/hevc") ||
                  !std::strcmp(mime, "video/x-vnd.on2.vp9") ||
                  !std::strcmp(mime, "video/av01"));
}

#if defined(__ANDROID__)
// AIMAGE_FORMAT_PRIVATE / HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED is 0x22.
// AHardwareBuffer intentionally does not expose an implementation-defined
// enumerator, but private ImageReader buffers can report this numeric format.
constexpr std::uint32_t kImplementationDefinedAhbFormat = 0x22u;

using ImageReaderNewWithUsageFn = media_status_t (*)(
    int32_t, int32_t, int32_t, uint64_t, int32_t, AImageReader**);
using ImageReaderAcquireAsyncFn = media_status_t (*)(
    AImageReader*, AImage**, int*);
using ImageGetHardwareBufferFn = media_status_t (*)(
    const AImage*, AHardwareBuffer**);
using HardwareBufferDescribeFn = void (*)(
    const AHardwareBuffer*, AHardwareBuffer_Desc*);

template <typename Fn>
Fn resolve_android_symbol(const char* name) noexcept {
  return reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, name));
}

struct AndroidApi26Symbols final {
  ImageReaderNewWithUsageFn image_reader_new_with_usage{};
  ImageReaderAcquireAsyncFn acquire_latest_image_async{};
  ImageReaderAcquireAsyncFn acquire_next_image_async{};
  ImageGetHardwareBufferFn image_get_hardware_buffer{};
  HardwareBufferDescribeFn hardware_buffer_describe{};

  [[nodiscard]] bool complete() const noexcept {
    return image_reader_new_with_usage && acquire_latest_image_async &&
           acquire_next_image_async && image_get_hardware_buffer &&
           hardware_buffer_describe;
  }
};

const AndroidApi26Symbols& android_api26_symbols() noexcept {
  static const AndroidApi26Symbols symbols = [] {
    AndroidApi26Symbols out{};
    out.image_reader_new_with_usage =
        resolve_android_symbol<ImageReaderNewWithUsageFn>(
            "AImageReader_newWithUsage");
    out.acquire_latest_image_async =
        resolve_android_symbol<ImageReaderAcquireAsyncFn>(
            "AImageReader_acquireLatestImageAsync");
    out.acquire_next_image_async =
        resolve_android_symbol<ImageReaderAcquireAsyncFn>(
            "AImageReader_acquireNextImageAsync");
    out.image_get_hardware_buffer =
        resolve_android_symbol<ImageGetHardwareBufferFn>(
            "AImage_getHardwareBuffer");
    out.hardware_buffer_describe =
        resolve_android_symbol<HardwareBufferDescribeFn>(
            "AHardwareBuffer_describe");
    return out;
  }();
  return symbols;
}
#endif

NativeMediaPixelFormat native_format(std::uint32_t format) noexcept {
#if defined(__ANDROID__)
#ifdef AHARDWAREBUFFER_FORMAT_YCbCr_P010
  if (format == AHARDWAREBUFFER_FORMAT_YCbCr_P010)
    return NativeMediaPixelFormat::p010;
#endif
  if (format == AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420 ||
      format == kImplementationDefinedAhbFormat)
    return NativeMediaPixelFormat::nv12;
#else
  (void)format;
#endif
  return NativeMediaPixelFormat::unknown;
}

} // namespace

struct AndroidMediaCodecAhbDecoder::Impl {
  explicit Impl(AndroidMediaCodecSessionConfig c) : config(std::move(c)) {}
  AndroidMediaCodecSessionConfig config;
  mutable std::mutex mutex;
  AndroidMediaCodecCapabilities caps;
  AndroidDecoderStatistics stats;
  std::string diagnostic_text{"not initialized"};
  std::atomic_bool cancelled{false};
  std::atomic_uint32_t in_flight{0};
  std::uint64_t generation{1}, identity{1};
#if defined(__ANDROID__)
  AMediaExtractor* extractor{};
  AMediaCodec* codec{};
  AImageReader* reader{};
  ANativeWindow* window{};
  AMediaFormat* track_format{};
  std::uint32_t width{}, height{};
  bool input_eos{}, output_eos{};

  void close() noexcept {
    if (codec) {
      AMediaCodec_stop(codec);
      AMediaCodec_delete(codec);
      codec = nullptr;
    }
    if (window) {
      ANativeWindow_release(window);
      window = nullptr;
    }
    if (reader) {
      AImageReader_delete(reader);
      reader = nullptr;
    }
    if (track_format) {
      AMediaFormat_delete(track_format);
      track_format = nullptr;
    }
    if (extractor) {
      AMediaExtractor_delete(extractor);
      extractor = nullptr;
    }
  }
#endif
  ~Impl() {
#if defined(__ANDROID__)
    close();
#endif
  }

  DigitorResult fail(DigitorResult r, const char* text) noexcept {
    std::scoped_lock lock(mutex);
    diagnostic_text = text;
    return r;
  }
};

AndroidMediaCodecAhbDecoder::AndroidMediaCodecAhbDecoder(
    AndroidMediaCodecSessionConfig c)
    : impl_(std::make_shared<Impl>(std::move(c))) {}
AndroidMediaCodecAhbDecoder::~AndroidMediaCodecAhbDecoder() { cancel(); }

DigitorResult AndroidMediaCodecAhbDecoder::initialize() noexcept {
  auto& i = *impl_;
  if (i.config.media_path.empty() || i.config.max_acquired_images < 2 ||
      i.config.max_acquired_images > 16 || !i.config.dequeue_timeout_us)
    return i.fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "invalid Android decoder configuration");
#if !defined(__ANDROID__)
  return i.fail(DIGITOR_RESULT_UNSUPPORTED,
                "NDK MediaCodec is available only on Android");
#else
  if (android_get_device_api_level() < 26)
    return i.fail(
        DIGITOR_RESULT_UNSUPPORTED,
        "API 26 is required for PRIVATE AImageReader AHardwareBuffer output");

  const auto& api26 = android_api26_symbols();
  if (!api26.complete())
    return i.fail(
        DIGITOR_RESULT_BACKEND_UNAVAILABLE,
        "required API 26 AImageReader/AHardwareBuffer symbols are unavailable");

  i.caps.ndk_media_codec = true;
  i.caps.image_reader_private_output = true;
  i.caps.ahardwarebuffer = true;
  i.caps.output_handle_type = "AHardwareBuffer";

  i.extractor = AMediaExtractor_new();
  if (!i.extractor ||
      AMediaExtractor_setDataSource(i.extractor, i.config.media_path.c_str()) !=
          AMEDIA_OK)
    return i.fail(DIGITOR_RESULT_NOT_INITIALIZED,
                  "AMediaExtractor could not open media source");

  const auto tracks = AMediaExtractor_getTrackCount(i.extractor);
  const char* mime = nullptr;
  for (size_t n = 0; n < tracks; ++n) {
    auto* f = AMediaExtractor_getTrackFormat(i.extractor, n);
    const char* candidate = nullptr;
    if (f &&
        AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &candidate) &&
        accepted_mime(candidate)) {
      i.track_format = f;
      mime = candidate;
      AMediaExtractor_selectTrack(i.extractor, n);
      break;
    }
    if (f) AMediaFormat_delete(f);
  }
  if (!i.track_format || !mime)
    return i.fail(DIGITOR_RESULT_UNSUPPORTED,
                  "no supported AVC, HEVC, VP9, or AV1 video track");

  int32_t w = 0, h = 0;
  if (!AMediaFormat_getInt32(i.track_format, AMEDIAFORMAT_KEY_WIDTH, &w) ||
      !AMediaFormat_getInt32(i.track_format, AMEDIAFORMAT_KEY_HEIGHT, &h) ||
      w <= 0 || h <= 0)
    return i.fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "video track has invalid dimensions");
  i.width = static_cast<std::uint32_t>(w);
  i.height = static_cast<std::uint32_t>(h);

  const media_status_t reader_status = api26.image_reader_new_with_usage(
      w, h, AIMAGE_FORMAT_PRIVATE, AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
      i.config.max_acquired_images, &i.reader);
  if (reader_status != AMEDIA_OK || !i.reader ||
      AImageReader_getWindow(i.reader, &i.window) != AMEDIA_OK || !i.window)
    return i.fail(
        DIGITOR_RESULT_BACKEND_UNAVAILABLE,
        "GPU-sampleable PRIVATE AImageReader surface creation failed");
  ANativeWindow_acquire(i.window);

  i.codec = AMediaCodec_createDecoderByType(mime);
  if (!i.codec)
    return i.fail(DIGITOR_RESULT_UNSUPPORTED,
                  "MediaCodec decoder unavailable for selected MIME type");
#if __ANDROID_API__ >= 28
  if (i.config.strict_zero_copy) {
    char* codec_name = nullptr;
    if (AMediaCodec_getName(i.codec, &codec_name) != AMEDIA_OK || !codec_name)
      return i.fail(
          DIGITOR_RESULT_UNSUPPORTED,
          "unable to qualify MediaCodec as a hardware decoder");
    std::string name(codec_name);
    AMediaCodec_releaseName(i.codec, codec_name);
    std::transform(
        name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.find("omx.google") != std::string::npos ||
        name.find("c2.android") != std::string::npos ||
        name.find("ffmpeg") != std::string::npos ||
        name.find("software") != std::string::npos)
      return i.fail(
          DIGITOR_RESULT_UNSUPPORTED,
          "strict zero-copy rejected a software MediaCodec component");
  }
#else
  if (i.config.strict_zero_copy)
    return i.fail(
        DIGITOR_RESULT_UNSUPPORTED,
        "this NDK API cannot qualify hardware codec identity in strict mode");
#endif

  if (AMediaCodec_configure(i.codec, i.track_format, i.window, nullptr, 0) !=
          AMEDIA_OK ||
      AMediaCodec_start(i.codec) != AMEDIA_OK)
    return i.fail(DIGITOR_RESULT_NOT_INITIALIZED,
                  "MediaCodec Surface configuration or start failed");

  i.caps.gpu_sampled_output = true;
  i.caps.supports_8_bit = true;
  i.caps.supports_avc = !std::strcmp(mime, "video/avc");
  i.caps.supports_hevc = !std::strcmp(mime, "video/hevc");
  i.caps.supports_vp9 = !std::strcmp(mime, "video/x-vnd.on2.vp9");
  i.caps.supports_av1 = !std::strcmp(mime, "video/av01");
  {
    std::scoped_lock lock(i.mutex);
    i.diagnostic_text =
        "NDK MediaCodec PRIVATE Surface decoder initialized";
  }
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidMediaCodecAhbDecoder::decode_next(
    NativeMediaSurfacePtr& out) noexcept {
  out.reset();
  auto self = impl_;
  auto& i = *self;
#if !defined(__ANDROID__)
  return i.fail(DIGITOR_RESULT_UNSUPPORTED,
                "NDK MediaCodec is available only on Android");
#else
  if (!i.codec || !i.reader)
    return i.fail(DIGITOR_RESULT_NOT_INITIALIZED,
                  "decoder is not initialized");
  if (i.cancelled.load())
    return i.fail(DIGITOR_RESULT_RESOURCE_IN_USE, "decoder cancelled");
  if (i.in_flight.load() >= i.config.max_acquired_images)
    return i.fail(
        DIGITOR_RESULT_RESOURCE_IN_USE,
        "AImageReader frame pool backpressure limit reached");

  const auto& api26 = android_api26_symbols();
  if (!api26.complete())
    return i.fail(
        DIGITOR_RESULT_BACKEND_UNAVAILABLE,
        "required API 26 AImageReader/AHardwareBuffer symbols are unavailable");

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (std::chrono::steady_clock::now() < deadline &&
         !i.cancelled.load()) {
    if (!i.input_eos) {
      const auto index =
          AMediaCodec_dequeueInputBuffer(i.codec, i.config.dequeue_timeout_us);
      if (index >= 0) {
        size_t capacity = 0;
        auto* bytes = AMediaCodec_getInputBuffer(i.codec, index, &capacity);
        const auto count =
            bytes ? AMediaExtractor_readSampleData(i.extractor, bytes, capacity)
                  : -1;
        if (count < 0) {
          AMediaCodec_queueInputBuffer(
              i.codec, index, 0, 0, 0,
              AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          i.input_eos = true;
          i.stats.eos_submitted = true;
        } else {
          const auto pts = AMediaExtractor_getSampleTime(i.extractor);
          AMediaCodec_queueInputBuffer(
              i.codec, index, 0, count, std::max<int64_t>(0, pts), 0);
          AMediaExtractor_advance(i.extractor);
          ++i.stats.submitted_samples;
        }
      }
    }

    AMediaCodecBufferInfo info{};
    const auto output_index =
        AMediaCodec_dequeueOutputBuffer(i.codec, &info,
                                        i.config.dequeue_timeout_us);
    if (output_index >= 0) {
      const bool eos =
          (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
      // Surface decoders may report zero byte count: the output buffer is a
      // render token, not pixel storage. Render every non-EOS token.
      const bool render = !eos;
      AMediaCodec_releaseOutputBuffer(i.codec, output_index, render);
      if (render) ++i.stats.rendered_outputs;
      if (eos) {
        i.output_eos = true;
        i.stats.eos_drained = true;
      }
    }

    AImage* image = nullptr;
    int fence_fd = -1;
    const media_status_t acquired =
        i.config.scheduling == AndroidDecodeScheduling::realtime_latest
            ? api26.acquire_latest_image_async(i.reader, &image, &fence_fd)
            : api26.acquire_next_image_async(i.reader, &image, &fence_fd);
    if (acquired == AMEDIA_OK && image) {
      AHardwareBuffer* ahb = nullptr;
      int64_t timestamp_ns = 0;
      AImage_getTimestamp(image, &timestamp_ns);
      if (api26.image_get_hardware_buffer(image, &ahb) != AMEDIA_OK || !ahb) {
        if (fence_fd >= 0) ::close(fence_fd);
        AImage_delete(image);
        return i.fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                      "AImage has no AHardwareBuffer");
      }

      AHardwareBuffer_Desc desc{};
      api26.hardware_buffer_describe(ahb, &desc);
      const auto format = native_format(desc.format);
      if (format == NativeMediaPixelFormat::unknown ||
          !(desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)) {
        if (fence_fd >= 0) ::close(fence_fd);
        AImage_delete(image);
        return i.fail(
            DIGITOR_RESULT_UNSUPPORTED,
            "decoder returned unsupported or non-GPU-sampleable hardware-buffer format");
      }
      if (desc.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) {
        if (fence_fd >= 0) ::close(fence_fd);
        AImage_delete(image);
        return i.fail(
            DIGITOR_RESULT_UNSUPPORTED,
            "protected decoder output is not supported by the editing pipeline");
      }

      i.in_flight.fetch_add(1);
      ++i.stats.acquired_images;
      struct Owner {
        AImage* image{};
        int fence{-1};
        std::shared_ptr<Impl> session;
        ~Owner() {
          if (fence >= 0) ::close(fence);
          if (image) AImage_delete(image);
          session->in_flight.fetch_sub(1);
        }
      };
      auto owner = std::make_shared<Owner>();
      owner->image = image;
      owner->fence = fence_fd;
      owner->session = self;

      NativeMediaSurfaceDescriptor d{};
      d.struct_size = sizeof(d);
      d.api_version = 1;
      d.platform = NativeMediaPlatform::android;
      d.handle_type = NativeMediaHandleType::ahardware_buffer;
      d.pixel_format = format;
      d.width = desc.width;
      d.height = desc.height;
      d.plane_count = 2;
      d.native_handle = reinterpret_cast<std::uintptr_t>(ahb);
      d.native_device = 0;
      d.allocation_size =
          static_cast<std::uint64_t>(desc.height) * desc.stride;
      d.timestamp_us = timestamp_ns / 1000;
      if (fence_fd >= 0) {
        d.acquire_sync.type = NativeMediaSyncType::sync_fd;
        d.acquire_sync.handle = static_cast<std::uintptr_t>(fence_fd);
      }
      d.color.full_range = 0;
      d.color.matrix = 1;
      d.color.primaries = 1;
      d.color.transfer = 1;

      out = std::make_shared<NativeMediaSurface>(d, owner);
      i.stats.in_flight_images = i.in_flight.load();
      return DIGITOR_RESULT_OK;
    }
    if (fence_fd >= 0) ::close(fence_fd);
    if (image) AImage_delete(image);

    if (i.output_eos)
      return i.fail(DIGITOR_RESULT_UNSUPPORTED,
                    "MediaCodec EOS drained");
  }

  ++i.stats.decoder_stalls;
  return i.cancelled.load()
             ? i.fail(DIGITOR_RESULT_RESOURCE_IN_USE, "decoder cancelled")
             : i.fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                      "decoder frame acquisition timeout");
#endif
}

DigitorResult AndroidMediaCodecAhbDecoder::seek(
    std::int64_t timestamp_us) noexcept {
  if (timestamp_us < 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
#if !defined(__ANDROID__)
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!i.codec || !i.extractor) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (AMediaCodec_flush(i.codec) != AMEDIA_OK)
    return i.fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                  "MediaCodec flush failed during seek");
  AMediaExtractor_seekTo(i.extractor, timestamp_us,
                         AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
  ++i.generation;
  i.input_eos = i.output_eos = false;
  i.stats.eos_submitted = i.stats.eos_drained = false;
  return DIGITOR_RESULT_OK;
#endif
}

DigitorResult AndroidMediaCodecAhbDecoder::flush() noexcept {
#if !defined(__ANDROID__)
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i = *impl_;
  if (!i.codec) return DIGITOR_RESULT_NOT_INITIALIZED;
  if (AMediaCodec_flush(i.codec) != AMEDIA_OK)
    return i.fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                  "MediaCodec flush failed");
  ++i.generation;
  i.input_eos = i.output_eos = false;
  return DIGITOR_RESULT_OK;
#endif
}

void AndroidMediaCodecAhbDecoder::cancel() noexcept {
  impl_->cancelled.store(true);
}

AndroidMediaCodecCapabilities
AndroidMediaCodecAhbDecoder::capabilities() const {
  std::scoped_lock l(impl_->mutex);
  return impl_->caps;
}

AndroidDecoderStatistics AndroidMediaCodecAhbDecoder::statistics() const {
  std::scoped_lock l(impl_->mutex);
  auto s = impl_->stats;
  s.in_flight_images = impl_->in_flight.load();
  return s;
}

const std::string& AndroidMediaCodecAhbDecoder::diagnostic() const noexcept {
  return impl_->diagnostic_text;
}

} // namespace digitor
