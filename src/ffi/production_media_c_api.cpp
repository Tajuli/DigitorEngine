#include "digitor/production_media_c_api.h"

#include "digitor/digitor.h"
#include "digitor/media.hpp"
#include "digitor/native_media.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#ifdef DIGITOR_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}
#endif

struct DigitorProductionMediaSource {
  std::unique_ptr<digitor::VideoDecoder> decoder;
  std::shared_ptr<digitor::VideoFrame> current_frame;
  int64_t duration_us{};
};

namespace {

digitor::HardwareDecode decode_mode(uint32_t value) {
  switch (value) {
    case DIGITOR_PRODUCTION_DECODE_CPU:
      return digitor::HardwareDecode::cpu;
    case DIGITOR_PRODUCTION_DECODE_DXVA:
      return digitor::HardwareDecode::dxva;
    case DIGITOR_PRODUCTION_DECODE_VIDEOTOOLBOX:
      return digitor::HardwareDecode::videotoolbox;
    case DIGITOR_PRODUCTION_DECODE_MEDIACODEC:
      return digitor::HardwareDecode::mediacodec;
    case DIGITOR_PRODUCTION_DECODE_AUTO:
    default:
      return digitor::HardwareDecode::automatic;
  }
}

uint32_t encode_mode(digitor::HardwareDecode value) {
  switch (value) {
    case digitor::HardwareDecode::cpu:
      return DIGITOR_PRODUCTION_DECODE_CPU;
    case digitor::HardwareDecode::dxva:
      return DIGITOR_PRODUCTION_DECODE_DXVA;
    case digitor::HardwareDecode::videotoolbox:
      return DIGITOR_PRODUCTION_DECODE_VIDEOTOOLBOX;
    case digitor::HardwareDecode::mediacodec:
      return DIGITOR_PRODUCTION_DECODE_MEDIACODEC;
    case digitor::HardwareDecode::automatic:
    default:
      return DIGITOR_PRODUCTION_DECODE_AUTO;
  }
}

template <typename Fn>
int32_t guard(Fn&& fn) noexcept {
  try {
    return static_cast<int32_t>(fn());
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (const std::invalid_argument&) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

void copy_string(char* output, size_t capacity, const std::string& value) noexcept {
  if (!output || capacity == 0) {
    return;
  }
  const auto count = std::min(capacity - 1, value.size());
  std::memcpy(output, value.data(), count);
  output[count] = '\0';
}

int64_t probe_duration_us(const char* path) noexcept {
#ifdef DIGITOR_HAS_FFMPEG
  AVFormatContext* format = nullptr;
  if (avformat_open_input(&format, path, nullptr, nullptr) < 0) {
    return 0;
  }
  int64_t duration_us = 0;
  if (avformat_find_stream_info(format, nullptr) >= 0) {
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
      duration_us = format->duration;
    } else {
      const int stream_index =
          av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
      if (stream_index >= 0) {
        const AVStream* stream = format->streams[stream_index];
        if (stream && stream->duration != AV_NOPTS_VALUE &&
            stream->duration > 0) {
          duration_us = av_rescale_q(
              stream->duration, stream->time_base, AVRational{1, 1000000});
        }
      }
    }
  }
  avformat_close_input(&format);
  return std::max<int64_t>(0, duration_us);
#else
  (void)path;
  return 0;
#endif
}

bool valid_options(const DigitorProductionMediaOptions* options) noexcept {
  if (!options) {
    return true;
  }
  return options->struct_size >= sizeof(DigitorProductionMediaOptions) &&
         options->api_version == DIGITOR_PRODUCTION_MEDIA_OPTIONS_VERSION &&
         options->hardware_decode <= DIGITOR_PRODUCTION_DECODE_MEDIACODEC;
}

bool valid_info_output(const DigitorProductionDecoderInfo* output) noexcept {
  return output && output->struct_size >= sizeof(DigitorProductionDecoderInfo) &&
         output->api_version == DIGITOR_PRODUCTION_DECODER_INFO_VERSION;
}

bool valid_frame_output(const DigitorProductionDecodedFrameInfo* output) noexcept {
  return output &&
         output->struct_size >= sizeof(DigitorProductionDecodedFrameInfo) &&
         output->api_version == DIGITOR_PRODUCTION_FRAME_INFO_VERSION;
}

bool valid_surface_output(
    const DigitorProductionNativeSurfaceDescriptor* output) noexcept {
  return output &&
         output->struct_size >=
             sizeof(DigitorProductionNativeSurfaceDescriptor) &&
         output->api_version == DIGITOR_PRODUCTION_NATIVE_SURFACE_VERSION;
}

}  // namespace

extern "C" {

int32_t digitor_production_media_open(
    const char* path,
    const DigitorProductionMediaOptions* options,
    DigitorProductionMediaSource** out_source) {
  if (!out_source) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  *out_source = nullptr;
  if (!path || path[0] == '\0' || !valid_options(options)) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
#if !defined(__ANDROID__)
  if (!digitor::ffmpeg_available()) {
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
#endif

  return guard([&]() -> DigitorResult {
    digitor::DecoderOptions resolved{};
    if (options) {
      resolved.hardware = decode_mode(options->hardware_decode);
      resolved.allow_cpu_fallback = options->allow_cpu_fallback != 0;
      resolved.cache_capacity =
          options->cache_capacity == 0 ? 16u : options->cache_capacity;
      resolved.require_zero_copy = options->require_zero_copy != 0;
      resolved.output_mode = resolved.require_zero_copy
                                 ? digitor::DecodeOutputMode::native_gpu_surface
                                 : digitor::DecodeOutputMode::automatic;
    }

    auto decoder = digitor::open_video_decoder(path, resolved);
    if (!decoder) {
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    auto source = std::make_unique<DigitorProductionMediaSource>();
    source->decoder = std::move(decoder);
    source->duration_us = probe_duration_us(path);
    *out_source = source.release();
    return DIGITOR_RESULT_OK;
  });
}

int32_t digitor_production_media_get_info(
    const DigitorProductionMediaSource* source,
    DigitorProductionDecoderInfo* out_info) {
  if (!source || !source->decoder || !valid_info_output(out_info)) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  return guard([&]() -> DigitorResult {
    const auto info = source->decoder->info();
    const auto struct_size = out_info->struct_size;
    const auto api_version = out_info->api_version;
    *out_info = {};
    out_info->struct_size = struct_size;
    out_info->api_version = api_version;
    out_info->selected_hardware_decode = encode_mode(info.selected);
    out_info->hardware_accelerated = info.hardware_accelerated ? 1u : 0u;
    out_info->native_surface_output = info.native_surface_output ? 1u : 0u;
    out_info->native_handle_type =
        static_cast<uint32_t>(info.native_handle_type);
    copy_string(out_info->implementation, sizeof(out_info->implementation),
                info.implementation);
    return DIGITOR_RESULT_OK;
  });
}

int32_t digitor_production_media_get_duration_us(
    const DigitorProductionMediaSource* source,
    int64_t* out_duration_us) {
  if (!source || !source->decoder || !out_duration_us) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  *out_duration_us = source->duration_us;
  return DIGITOR_RESULT_OK;
}

int32_t digitor_production_media_seek(
    DigitorProductionMediaSource* source,
    int64_t pts_us) {
  if (!source || !source->decoder || pts_us < 0) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  return guard([&]() -> DigitorResult {
    source->current_frame.reset();
    source->decoder->seek(pts_us);
    return DIGITOR_RESULT_OK;
  });
}

int32_t digitor_production_media_decode(
    DigitorProductionMediaSource* source,
    int64_t frame_number,
    DigitorProductionDecodedFrameInfo* out_frame) {
  if (!source || !source->decoder || frame_number < 0 ||
      !valid_frame_output(out_frame)) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  return guard([&]() -> DigitorResult {
    auto frame = source->decoder->decode(frame_number);
    if (!frame) {
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    source->current_frame = frame;
    const auto struct_size = out_frame->struct_size;
    const auto api_version = out_frame->api_version;
    *out_frame = {};
    out_frame->struct_size = struct_size;
    out_frame->api_version = api_version;
    out_frame->frame_number = frame->number;
    out_frame->pts_us = frame->pts;
    out_frame->duration_us = frame->duration;
    out_frame->width = frame->width;
    out_frame->height = frame->height;
    out_frame->pixel_format = static_cast<uint32_t>(frame->pixel_format);
    out_frame->gpu_resident = frame->gpu_resident() ? 1u : 0u;
    out_frame->cpu_resident = frame->cpu_resident() ? 1u : 0u;
    return DIGITOR_RESULT_OK;
  });
}

int32_t digitor_production_media_get_native_surface(
    const DigitorProductionMediaSource* source,
    DigitorProductionNativeSurfaceDescriptor* out_surface) {
  if (!source || !source->decoder || !valid_surface_output(out_surface)) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!source->current_frame || !source->current_frame->native_surface) {
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  return guard([&]() -> DigitorResult {
    const auto& descriptor =
        source->current_frame->native_surface->descriptor();
    const auto struct_size = out_surface->struct_size;
    const auto api_version = out_surface->api_version;
    *out_surface = {};
    out_surface->struct_size = struct_size;
    out_surface->api_version = api_version;
    out_surface->platform = static_cast<uint32_t>(descriptor.platform);
    out_surface->handle_type = static_cast<uint32_t>(descriptor.handle_type);
    out_surface->pixel_format = static_cast<uint32_t>(descriptor.pixel_format);
    out_surface->width = descriptor.width;
    out_surface->height = descriptor.height;
    out_surface->plane_count = descriptor.plane_count;
    out_surface->array_slice = descriptor.array_slice;
    out_surface->native_handle =
        static_cast<uint64_t>(descriptor.native_handle);
    out_surface->native_device =
        static_cast<uint64_t>(descriptor.native_device);
    out_surface->allocation_size = descriptor.allocation_size;
    out_surface->timestamp_us = descriptor.timestamp_us;
    out_surface->acquire_sync_type =
        static_cast<uint32_t>(descriptor.acquire_sync.type);
    out_surface->acquire_sync_handle =
        static_cast<uint64_t>(descriptor.acquire_sync.handle);
    out_surface->acquire_sync_value = descriptor.acquire_sync.value;
    out_surface->color_primaries = descriptor.color.primaries;
    out_surface->transfer_function = descriptor.color.transfer;
    out_surface->matrix_coefficients = descriptor.color.matrix;
    out_surface->full_range = descriptor.color.full_range;
    out_surface->chroma_location = descriptor.color.chroma_location;
    return DIGITOR_RESULT_OK;
  });
}

void digitor_production_media_close(DigitorProductionMediaSource* source) {
  delete source;
}

uint8_t digitor_production_media_ffmpeg_available(void) {
  return digitor::ffmpeg_available() ? 1u : 0u;
}

}  // extern "C"