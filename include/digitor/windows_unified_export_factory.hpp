#pragma once

#include "digitor/flutter_production_c_api.h"
#include "digitor/production_encoder_factory.hpp"
#include "digitor/media.hpp"
#include "digitor/windows_d3d12_p010_converter.hpp"
#include "digitor/windows_d3d12_p010_dispatch.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <mutex>
#include <string>
#include <utility>

namespace digitor {
namespace windows_unified_export_detail {

using Microsoft::WRL::ComPtr;

using TextureDescriptorBuilder = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, std::uint64_t,
    DigitorNativeGpuTextureDescriptor&, std::string&)>;

class MediaSourcePathState final {
 public:
  void set(std::string value) {
    std::scoped_lock lock(mutex_);
    path_ = std::move(value);
  }
  [[nodiscard]] std::string get() const {
    std::scoped_lock lock(mutex_);
    return path_;
  }

 private:
  mutable std::mutex mutex_;
  std::string path_;
};

struct State final {
  TextureDescriptorBuilder descriptor_builder;
  std::shared_ptr<const ExportRenderSnapshot> snapshot;
  std::string source_media_path;
  std::mutex mutex;
  HardwareEncodeConfig config;
  std::filesystem::path final_path;
  std::filesystem::path staged_path;

  bool opened{};
  bool initialized{};
  bool cancelled{};
  bool finalized{};
  bool mf_started{};
  bool audio_enabled{};
  bool no_cpu_pixels{true};
  bool synchronization_waited{};
  bool native_resource_registered{};
  std::uint64_t video_frames{};
  std::uint64_t audio_sample_frames{};
  std::int64_t timeline_origin_us{-1};

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device12;
  ComPtr<ID3D12CommandQueue> queue12;
  ComPtr<ID3D11Device> device11;
  ComPtr<IMFDXGIDeviceManager> device_manager;
  UINT device_manager_token{};

  std::unique_ptr<WindowsD3D12P010Dispatch> dispatch;
  std::unique_ptr<WindowsD3D12P010Converter> converter;
  std::unique_ptr<AudioDecoder> audio_decoder;

  ComPtr<IMFSinkWriter> writer;
  DWORD video_stream{};
  DWORD audio_stream{};
  std::uint32_t audio_sample_rate{};
  std::uint32_t audio_channels{};

  ~State() {
    writer.Reset();
    if (mf_started) {
      MFShutdown();
      mf_started = false;
    }
  }
};

inline std::filesystem::path staged_output_path(
    const std::filesystem::path& final_path) {
  const auto stem = final_path.stem().string() + ".digitor-partial";
  return final_path.parent_path() / (stem + final_path.extension().string());
}

inline bool no_audio_stream_error(const std::exception& error) {
  return std::string(error.what()).find("no decodable audio stream") !=
         std::string::npos;
}

inline DigitorResult open_same_adapter_devices(
    State& state, HANDLE shared, std::uint32_t width, std::uint32_t height,
    std::string& diagnostic) {
  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    diagnostic = "DXGI factory creation failed for unified production export";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;

    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

    ComPtr<ID3D12Device> device12;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device12)))) {
      continue;
    }

    ComPtr<ID3D12Resource> probe;
    if (FAILED(device12->OpenSharedHandle(shared, IID_PPV_ARGS(&probe)))) {
      continue;
    }
    const auto resource_desc = probe->GetDesc();
    if (resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        resource_desc.Width != width || resource_desc.Height != height ||
        resource_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
      continue;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    ComPtr<ID3D12CommandQueue> queue12;
    if (FAILED(device12->CreateCommandQueue(&queue_desc,
                                            IID_PPV_ARGS(&queue12)))) {
      continue;
    }

    const UINT flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                        D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    D3D_FEATURE_LEVEL selected{};
    if (FAILED(D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 2,
            D3D11_SDK_VERSION, &device11, &selected, &context11))) {
      continue;
    }

    ComPtr<IMFDXGIDeviceManager> manager;
    UINT token{};
    if (FAILED(MFCreateDXGIDeviceManager(&token, &manager)) ||
        FAILED(manager->ResetDevice(device11.Get(), token))) {
      continue;
    }

    state.adapter = std::move(adapter);
    state.device12 = std::move(device12);
    state.queue12 = std::move(queue12);
    state.device11 = std::move(device11);
    state.device_manager = std::move(manager);
    state.device_manager_token = token;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }

  diagnostic =
      "no hardware adapter could open the final graded D3D12 shared resource";
  return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

inline DigitorResult configure_audio_stream(State& state,
                                            std::string& diagnostic) {
  if (state.source_media_path.empty()) {
    state.audio_enabled = false;
    return DIGITOR_RESULT_OK;
  }

  std::shared_ptr<AudioFrame> probe;
  try {
    state.audio_decoder = open_audio_decoder(state.source_media_path);
    probe = state.audio_decoder ? state.audio_decoder->decode(0) : nullptr;
  } catch (const std::exception& error) {
    if (no_audio_stream_error(error)) {
      state.audio_decoder.reset();
      state.audio_enabled = false;
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    }
    diagnostic =
        std::string("source audio decoder initialization failed: ") +
        error.what();
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  } catch (...) {
    diagnostic = "source audio decoder initialization failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  if (!probe || probe->sample_rate == 0 || probe->channels == 0 ||
      probe->samples.empty() ||
      probe->samples.size() % probe->channels != 0) {
    diagnostic = "source audio stream did not produce valid float PCM";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  state.audio_sample_rate = probe->sample_rate;
  state.audio_channels = probe->channels;
  if (state.audio_channels > 2) {
    diagnostic = "source audio channel count exceeds stereo Media Foundation AAC path";
    return DIGITOR_RESULT_UNSUPPORTED;
  }

  ComPtr<IMFMediaType> output;
  if (FAILED(MFCreateMediaType(&output)) ||
      FAILED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
      FAILED(output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC)) ||
      FAILED(output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                               state.audio_channels)) ||
      FAILED(output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                               state.audio_sample_rate)) ||
      FAILED(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24'000)) ||
      FAILED(output->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0)) ||
      FAILED(output->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,
                               0x29)) ||
      FAILED(state.writer->AddStream(output.Get(), &state.audio_stream))) {
    diagnostic = "failed to configure AAC output stream";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  const auto block_alignment = state.audio_channels * sizeof(std::int16_t);
  if (block_alignment >
      static_cast<std::uint64_t>((std::numeric_limits<UINT32>::max)())) {
    diagnostic = "audio block alignment is out of range";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  ComPtr<IMFMediaType> input;
  if (FAILED(MFCreateMediaType(&input)) ||
      FAILED(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
      FAILED(input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
      FAILED(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                              state.audio_channels)) ||
      FAILED(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                              state.audio_sample_rate)) ||
      FAILED(input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
      FAILED(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
                              static_cast<UINT32>(block_alignment))) ||
      FAILED(input->SetUINT32(
          MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
          static_cast<UINT32>(state.audio_sample_rate * block_alignment))) ||
      FAILED(input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE)) ||
      FAILED(state.writer->SetInputMediaType(state.audio_stream, input.Get(),
                                             nullptr))) {
    diagnostic = "failed to configure PCM audio input";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  state.audio_decoder->seek(0);
  state.audio_enabled = true;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

inline DigitorResult initialize_writer(
    State& state, const DigitorNativeGpuTextureDescriptor& descriptor,
    std::string& diagnostic) {
  if (state.initialized) return DIGITOR_RESULT_OK;

  auto result = open_same_adapter_devices(
      state, reinterpret_cast<HANDLE>(descriptor.native_handle),
      descriptor.width, descriptor.height, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;

  WindowsD3D12P010DispatchConfig dispatch_config{};
  dispatch_config.device = state.device12.Get();
  dispatch_config.input_format = DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  dispatch_config.source_starts_shader_readable = false;
  state.dispatch =
      std::make_unique<WindowsD3D12P010Dispatch>(std::move(dispatch_config));
  result = state.dispatch->initialize();
  if (result != DIGITOR_RESULT_OK) {
    diagnostic = "embedded D3D12 RGBA-to-P010 dispatch initialization failed";
    return result;
  }

  WindowsP010ConversionConfig conversion{};
  conversion.d3d12_device = state.device12.Get();
  conversion.command_queue = state.queue12.Get();
  conversion.d3d11_device = state.device11.Get();
  conversion.width = descriptor.width;
  conversion.height = descriptor.height;
  conversion.input_format = DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  conversion.input_transfer_encoded = true;
  conversion.matrix = state.snapshot->data().hdr
                          ? WindowsOutputMatrix::bt2020_ncl
                          : WindowsOutputMatrix::bt709;
  conversion.transfer = state.snapshot->data().hdr
                            ? WindowsOutputTransfer::pq
                            : WindowsOutputTransfer::gamma24;
  conversion.mastering_peak_nits =
      state.snapshot->data().hdr ? 1000.0f : 100.0f;
  conversion.gpu_dispatch = state.dispatch->callback();
  state.converter =
      std::make_unique<WindowsD3D12P010Converter>(std::move(conversion));
  result = state.converter->initialize();
  if (result != DIGITOR_RESULT_OK) {
    diagnostic = "shared D3D12/D3D11 P010 converter initialization failed";
    return result;
  }

  if (FAILED(MFStartup(MF_VERSION))) {
    diagnostic = "Media Foundation startup failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  state.mf_started = true;

  const auto staged_utf8 = state.staged_path.string();
  const int required = MultiByteToWideChar(
      CP_UTF8, 0, staged_utf8.c_str(), -1, nullptr, 0);
  if (required <= 0) {
    diagnostic = "Windows export path is not valid UTF-8";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  std::wstring output_path(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, staged_utf8.c_str(), -1,
                          output_path.data(), required) <= 0) {
    diagnostic = "failed to convert Windows export path";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  ComPtr<IMFAttributes> attributes;
  if (FAILED(MFCreateAttributes(&attributes, 3)) ||
      FAILED(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
                                   TRUE)) ||
      FAILED(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE)) ||
      FAILED(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,
                                    state.device_manager.Get())) ||
      FAILED(MFCreateSinkWriterFromURL(output_path.c_str(), nullptr,
                                       attributes.Get(), &state.writer))) {
    diagnostic = "Media Foundation sink writer creation failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<IMFMediaType> video_output;
  if (FAILED(MFCreateMediaType(&video_output)) ||
      FAILED(video_output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(video_output->SetGUID(
          MF_MT_SUBTYPE, state.config.profile.codec == ExportCodec::hevc
                             ? MFVideoFormat_HEVC
                             : MFVideoFormat_H264)) ||
      FAILED(video_output->SetUINT32(
          MF_MT_AVG_BITRATE,
          static_cast<UINT32>(state.config.profile.video_bitrate))) ||
      FAILED(MFSetAttributeSize(video_output.Get(), MF_MT_FRAME_SIZE,
                                descriptor.width, descriptor.height)) ||
      FAILED(MFSetAttributeRatio(
          video_output.Get(), MF_MT_FRAME_RATE,
          static_cast<UINT32>(state.config.profile.fps_num),
          static_cast<UINT32>(state.config.profile.fps_den))) ||
      FAILED(MFSetAttributeRatio(video_output.Get(), MF_MT_PIXEL_ASPECT_RATIO,
                                 1, 1)) ||
      FAILED(state.writer->AddStream(video_output.Get(),
                                     &state.video_stream))) {
    diagnostic = "failed to configure hardware video output stream";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<IMFMediaType> video_input;
  if (FAILED(MFCreateMediaType(&video_input)) ||
      FAILED(video_input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(video_input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_P010)) ||
      FAILED(MFSetAttributeSize(video_input.Get(), MF_MT_FRAME_SIZE,
                                descriptor.width, descriptor.height)) ||
      FAILED(MFSetAttributeRatio(
          video_input.Get(), MF_MT_FRAME_RATE,
          static_cast<UINT32>(state.config.profile.fps_num),
          static_cast<UINT32>(state.config.profile.fps_den))) ||
      FAILED(MFSetAttributeRatio(video_input.Get(), MF_MT_PIXEL_ASPECT_RATIO,
                                 1, 1)) ||
      FAILED(video_input->SetUINT32(MF_MT_INTERLACE_MODE,
                                    MFVideoInterlace_Progressive)) ||
      FAILED(state.writer->SetInputMediaType(state.video_stream,
                                             video_input.Get(), nullptr))) {
    diagnostic = "failed to configure P010 hardware video input";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  result = configure_audio_stream(state, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;

  if (FAILED(state.writer->BeginWriting())) {
    diagnostic = "Media Foundation sink writer could not begin";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  state.initialized = true;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

inline DigitorResult write_video_sample(
    State& state, const HardwareEncodeFrame& input,
    const DigitorNativeGpuTextureDescriptor& descriptor,
    std::string& diagnostic) {
  ComPtr<ID3D12Resource> shared;
  if (FAILED(state.device12->OpenSharedHandle(
          reinterpret_cast<HANDLE>(descriptor.native_handle),
          IID_PPV_ARGS(&shared)))) {
    diagnostic =
        "hardware encoder could not open final output-transform resource";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  WindowsD3D12FrameLease lease;
  lease.resource = shared.Get();
  lease.width = descriptor.width;
  lease.height = descriptor.height;
  lease.format = DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  lease.timestamp_us = input.pts_us;
  lease.frame_identity = input.frame->identity();
  lease.consumer = WindowsNativeConsumerKind::hardware_encoder;
  lease.release = [shared = std::move(shared), frame = input.frame]() mutable {
    shared.Reset();
    (void)frame;
  };

  WindowsP010EncoderSurface surface;
  auto result = state.converter->convert(lease, surface);
  if (result != DIGITOR_RESULT_OK) {
    diagnostic = "GPU RGBA-to-P010 conversion failed";
    return result;
  }
  if (!surface.resource || surface.width != descriptor.width ||
      surface.height != descriptor.height ||
      surface.timestamp_us != input.pts_us ||
      surface.frame_identity != input.frame->identity()) {
    diagnostic = "P010 surface identity/timestamp differs from final GPU frame";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }

  ComPtr<IMFMediaBuffer> buffer;
  auto* texture = static_cast<ID3D11Texture2D*>(surface.resource);
  if (FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0,
                                       FALSE, &buffer))) {
    diagnostic = "Media Foundation could not wrap P010 GPU surface";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<IMFSample> sample;
  if (FAILED(MFCreateSample(&sample)) ||
      FAILED(sample->AddBuffer(buffer.Get()))) {
    diagnostic = "Media Foundation video sample creation failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  if (state.timeline_origin_us < 0) state.timeline_origin_us = input.pts_us;
  if (input.pts_us < state.timeline_origin_us || input.duration_us <= 0) {
    diagnostic = "video timestamp is before unified export timeline origin";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  const auto normalized_pts = input.pts_us - state.timeline_origin_us;
  if (normalized_pts > (std::numeric_limits<LONGLONG>::max)() / 10 ||
      input.duration_us > (std::numeric_limits<LONGLONG>::max)() / 10) {
    diagnostic = "video timestamp exceeds Media Foundation range";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  if (FAILED(sample->SetSampleTime(normalized_pts * 10)) ||
      FAILED(sample->SetSampleDuration(input.duration_us * 10)) ||
      FAILED(state.writer->WriteSample(state.video_stream, sample.Get()))) {
    diagnostic = "Media Foundation rejected final graded GPU video sample";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ++state.video_frames;
  state.synchronization_waited = true;
  state.native_resource_registered = true;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

inline DigitorResult write_audio_range(State& state, std::string& diagnostic) {
  if (!state.audio_enabled) return DIGITOR_RESULT_OK;
  if (!state.audio_decoder || state.timeline_origin_us < 0 ||
      state.config.duration_us <= 0) {
    diagnostic = "unified audio export is not initialized";
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }

  try {
    state.audio_decoder->seek(state.timeline_origin_us);
    const auto end_us = state.timeline_origin_us + state.config.duration_us;
    if (end_us < state.timeline_origin_us) {
      diagnostic = "audio export range overflows";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    FrameNumber frame_number = 0;
    for (;;) {
      auto frame = state.audio_decoder->decode(frame_number++);
      if (!frame) break;
      if (frame->sample_rate != state.audio_sample_rate ||
          frame->channels != state.audio_channels || frame->channels == 0 ||
          frame->samples.size() % frame->channels != 0) {
        diagnostic = "audio format changed during frozen production export";
        return DIGITOR_RESULT_UNSUPPORTED;
      }

      const auto total_sample_frames = frame->samples.size() / frame->channels;
      if (total_sample_frames == 0) continue;

      const auto source_duration_us = static_cast<std::int64_t>(
          (total_sample_frames * 1'000'000ULL) / frame->sample_rate);
      const auto frame_end_us = frame->pts + source_duration_us;
      if (frame_end_us <= state.timeline_origin_us) continue;
      if (frame->pts >= end_us) break;

      std::uint64_t first = 0;
      if (frame->pts < state.timeline_origin_us) {
        const auto delta = static_cast<std::uint64_t>(
            state.timeline_origin_us - frame->pts);
        first =
            (delta * frame->sample_rate + 999'999ULL) / 1'000'000ULL;
        first = std::min<std::uint64_t>(first, total_sample_frames);
      }

      std::uint64_t last = total_sample_frames;
      if (frame_end_us > end_us) {
        if (end_us <= frame->pts) break;
        const auto delta = static_cast<std::uint64_t>(end_us - frame->pts);
        last = (delta * frame->sample_rate) / 1'000'000ULL;
        last = std::min<std::uint64_t>(last, total_sample_frames);
      }
      if (last <= first) continue;

      const auto count = last - first;
      const auto sample_count = count * frame->channels;
      if (sample_count >
          (std::numeric_limits<DWORD>::max)() / sizeof(std::int16_t)) {
        diagnostic = "audio sample buffer exceeds Media Foundation limit";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      const auto bytes =
          static_cast<DWORD>(sample_count * sizeof(std::int16_t));

      ComPtr<IMFMediaBuffer> buffer;
      if (FAILED(MFCreateMemoryBuffer(bytes, &buffer))) {
        diagnostic = "Media Foundation audio buffer allocation failed";
        return DIGITOR_RESULT_OUT_OF_MEMORY;
      }

      BYTE* destination = nullptr;
      DWORD maximum = 0;
      DWORD current = 0;
      if (FAILED(buffer->Lock(&destination, &maximum, &current)) ||
          !destination || maximum < bytes) {
        if (destination) buffer->Unlock();
        diagnostic = "Media Foundation audio buffer lock failed";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      auto* pcm = reinterpret_cast<std::int16_t*>(destination);
      const auto source_offset = first * frame->channels;
      for (std::uint64_t sample_index = 0; sample_index < sample_count;
           ++sample_index) {
        const auto value = std::clamp(
            frame->samples[source_offset + sample_index], -1.0f, 1.0f);
        pcm[sample_index] =
            static_cast<std::int16_t>(value * 32767.0f);
      }
      buffer->Unlock();
      if (FAILED(buffer->SetCurrentLength(bytes))) {
        diagnostic = "Media Foundation audio buffer length failed";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }

      ComPtr<IMFSample> sample;
      if (FAILED(MFCreateSample(&sample)) ||
          FAILED(sample->AddBuffer(buffer.Get()))) {
        diagnostic = "Media Foundation audio sample creation failed";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }

      const auto first_offset_us = static_cast<std::int64_t>(
          (first * 1'000'000ULL) / frame->sample_rate);
      auto normalized_pts =
          frame->pts + first_offset_us - state.timeline_origin_us;
      normalized_pts = std::max<std::int64_t>(0, normalized_pts);
      const auto duration_100ns = static_cast<LONGLONG>(
          (count * 10'000'000ULL) / frame->sample_rate);
      if (normalized_pts > (std::numeric_limits<LONGLONG>::max)() / 10 ||
          duration_100ns <= 0 ||
          FAILED(sample->SetSampleTime(normalized_pts * 10)) ||
          FAILED(sample->SetSampleDuration(duration_100ns)) ||
          FAILED(state.writer->WriteSample(state.audio_stream,
                                           sample.Get()))) {
        diagnostic = "Media Foundation rejected synchronized AAC input";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }

      state.audio_sample_frames += count;
      if (frame_end_us >= end_us) break;
    }

    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    diagnostic = "out of memory decoding synchronized export audio";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (const std::exception& error) {
    diagnostic = std::string("source audio decode failed during export: ") +
                 error.what();
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  } catch (...) {
    diagnostic = "source audio decode failed during export";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

inline void abort_writer(State& state) noexcept {
  state.writer.Reset();
  if (state.mf_started) {
    MFShutdown();
    state.mf_started = false;
  }
  state.initialized = false;
}

}  // namespace windows_unified_export_detail

[[nodiscard]] inline ProductionEncoderFactoryResult
create_windows_unified_export_encoder(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    std::string source_media_path,
    windows_unified_export_detail::TextureDescriptorBuilder descriptor_builder) {
  using namespace windows_unified_export_detail;

  ProductionEncoderFactoryResult result{};
  if (!snapshot) {
    result.diagnostic = "missing immutable export snapshot";
    return result;
  }
  const auto& data = snapshot->data();
  if (data.renderer_backend != DIGITOR_RENDERER_D3D12) {
    result.diagnostic = "unified Windows exporter requires D3D12 final frames";
    return result;
  }
  if (data.encoder_backend != EncoderBackend::quick_sync) {
    result.diagnostic =
        "unified Windows Media Foundation exporter requires quick_sync backend";
    return result;
  }
  if (data.profile.codec != ExportCodec::h264 &&
      data.profile.codec != ExportCodec::hevc) {
    result.diagnostic =
        "unified Windows exporter currently supports H.264 and HEVC";
    return result;
  }
  if (data.output_path.empty() || !descriptor_builder) {
    result.diagnostic =
        "unified Windows exporter requires output path and descriptor builder";
    return result;
  }
  if (source_media_path.empty()) {
    result.diagnostic =
        "unified Windows exporter has no currently-open media source";
    return result;
  }

  auto state = std::make_shared<State>();
  state->snapshot = std::move(snapshot);
  state->source_media_path = std::move(source_media_path);
  state->descriptor_builder = std::move(descriptor_builder);

  result.callbacks.open =
      [state](const HardwareEncodeConfig& config,
              std::string& diagnostic) noexcept {
        try {
          std::scoped_lock lock(state->mutex);
          if (state->opened) {
            diagnostic = "unified Windows production encoder already opened";
            return DIGITOR_RESULT_RESOURCE_IN_USE;
          }
          if (config.backend != EncoderBackend::quick_sync ||
              !config.require_hardware || !config.require_zero_copy ||
              !config.require_atomic_finalize ||
              config.output_path.empty() || config.duration_us <= 0) {
            diagnostic =
                "unified Windows export requires hardware zero-copy atomic mode";
            return DIGITOR_RESULT_UNSUPPORTED;
          }
          state->config = config;
          state->final_path = config.output_path;
          state->staged_path = staged_output_path(state->final_path);
          std::error_code error;
          if (!state->final_path.parent_path().empty()) {
            std::filesystem::create_directories(
                state->final_path.parent_path(), error);
            if (error) {
              diagnostic = "failed to create Windows export directory";
              return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            }
          }
          std::filesystem::remove(state->staged_path, error);
          error.clear();
          state->opened = true;
          state->cancelled = false;
          diagnostic.clear();
          return DIGITOR_RESULT_OK;
        } catch (const std::bad_alloc&) {
          diagnostic = "out of memory opening unified Windows exporter";
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        } catch (...) {
          diagnostic = "failed to open unified Windows production encoder";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
      };

  result.callbacks.submit_gpu_frame =
      [state](const HardwareEncodeFrame& input,
              std::string& diagnostic) noexcept {
        try {
          std::scoped_lock lock(state->mutex);
          if (!state->opened || state->cancelled || !input.frame) {
            diagnostic = "unified Windows production encoder is not active";
            return DIGITOR_RESULT_NOT_INITIALIZED;
          }

          const auto& metadata = input.frame->metadata();
          const auto& frozen = state->snapshot->data();
          if (input.frame->backend() != DIGITOR_RENDERER_D3D12 ||
              !input.frame->ready() || !input.frame->context_live() ||
              metadata.width != frozen.width ||
              metadata.height != frozen.height ||
              metadata.color_metadata != frozen.color_metadata ||
              (metadata.format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT &&
               metadata.format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)) {
            diagnostic =
                "final graded GPU frame differs from frozen export contract";
            return DIGITOR_RESULT_INVALID_ARGUMENT;
          }

          DigitorNativeGpuTextureDescriptor descriptor{};
          descriptor.struct_size = sizeof(descriptor);
          descriptor.api_version =
              DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
          const auto generation =
              input.frame->identity() ? input.frame->identity() : 1;
          auto encode_result = state->descriptor_builder(
              input.frame, generation, descriptor, diagnostic);
          if (encode_result != DIGITOR_RESULT_OK) return encode_result;

          if (descriptor.backend != DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12 ||
              descriptor.handle_type !=
                  DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE ||
              descriptor.pixel_format != DIGITOR_PIXEL_FORMAT_RGBA8_UNORM ||
              !descriptor.native_handle ||
              descriptor.width != frozen.width ||
              descriptor.height != frozen.height) {
            diagnostic =
                "Windows output transform is not the required shared D3D12 RGBA8 resource";
            return DIGITOR_RESULT_UNSUPPORTED;
          }

          encode_result = initialize_writer(*state, descriptor, diagnostic);
          if (encode_result != DIGITOR_RESULT_OK) {
            abort_writer(*state);
            return encode_result;
          }

          encode_result =
              write_video_sample(*state, input, descriptor, diagnostic);
          if (encode_result != DIGITOR_RESULT_OK) return encode_result;

          diagnostic.clear();
          return DIGITOR_RESULT_OK;
        } catch (const std::bad_alloc&) {
          diagnostic = "out of memory encoding final Windows GPU frame";
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        } catch (...) {
          diagnostic = "unexpected unified Windows final-frame encode failure";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
      };

  result.callbacks.drain = [state](std::string& diagnostic) noexcept {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled || !state->initialized) {
      diagnostic = "unified Windows production encoder cannot drain";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };

  result.callbacks.finalize_atomic =
      [state](std::string& diagnostic) noexcept {
        try {
          std::scoped_lock lock(state->mutex);
          if (!state->opened || state->cancelled || !state->initialized ||
              !state->writer || state->video_frames == 0) {
            diagnostic =
                "unified Windows encoder has no completed video frames";
            return DIGITOR_RESULT_NOT_INITIALIZED;
          }

          auto finish_result = write_audio_range(*state, diagnostic);
          if (finish_result != DIGITOR_RESULT_OK) {
            abort_writer(*state);
            std::error_code error;
            std::filesystem::remove(state->staged_path, error);
            return finish_result;
          }

          const auto finalize_hr = state->writer->Finalize();
          state->writer.Reset();
          if (state->mf_started) {
            MFShutdown();
            state->mf_started = false;
          }
          state->initialized = false;
          if (FAILED(finalize_hr)) {
            diagnostic = "Media Foundation A/V export finalization failed";
            std::error_code error;
            std::filesystem::remove(state->staged_path, error);
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }

          std::error_code error;
          std::filesystem::remove(state->final_path, error);
          error.clear();
          std::filesystem::rename(state->staged_path, state->final_path,
                                  error);
          if (error) {
            diagnostic =
                "failed to atomically publish synchronized Windows export";
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }

          state->finalized = true;
          diagnostic.clear();
          return DIGITOR_RESULT_OK;
        } catch (const std::bad_alloc&) {
          diagnostic = "out of memory finalizing unified Windows export";
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        } catch (...) {
          diagnostic = "unexpected unified Windows export finalization failure";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
      };

  result.callbacks.cancel = [state]() noexcept {
    std::scoped_lock lock(state->mutex);
    if (state->cancelled) return;
    state->cancelled = true;
    abort_writer(*state);
    std::error_code error;
    std::filesystem::remove(state->staged_path, error);
  };

  result.zero_copy_qualified = [state]() {
    std::scoped_lock lock(state->mutex);
    const bool audio_ok =
        !state->audio_enabled || state->audio_sample_frames > 0;
    return state->finalized && state->video_frames > 0 &&
           state->no_cpu_pixels && state->synchronization_waited &&
           state->native_resource_registered && audio_ok;
  };
  result.windows_vulkan_interop_qualified = false;
  result.diagnostic.clear();
  return result;
}

[[nodiscard]] inline ProductionEncoderFactory
make_windows_unified_export_factory(
    windows_unified_export_detail::TextureDescriptorBuilder descriptor_builder,
    std::function<std::string()> source_media_path_getter) {
  if (!descriptor_builder || !source_media_path_getter) return {};
  return [descriptor_builder = std::move(descriptor_builder),
          source_media_path_getter = std::move(source_media_path_getter)](
             std::shared_ptr<const ExportRenderSnapshot> snapshot) {
    return create_windows_unified_export_encoder(
        std::move(snapshot), source_media_path_getter(), descriptor_builder);
  };
}

}  // namespace digitor

#endif  // defined(_WIN32)
