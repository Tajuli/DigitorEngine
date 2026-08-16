#include "digitor/windows_unified_export_factory.hpp"

#if defined(_WIN32)

#include "digitor/production_audio_media_pipeline.hpp"
#include "digitor/windows_d3d12_p010_converter.hpp"
#include "digitor/windows_d3d12_p010_dispatch.hpp"

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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace digitor {
namespace windows_unified_export_detail {

using Microsoft::WRL::ComPtr;

void MediaSourcePathState::set(std::string value) {
  std::scoped_lock lock(mutex_);
  path_ = std::move(value);
}

std::string MediaSourcePathState::get() const {
  std::scoped_lock lock(mutex_);
  return path_;
}

namespace {

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
  bool audio_revision_locked{};
  bool audio_source_rendered{};
  bool no_cpu_pixels{true};
  bool synchronization_waited{};
  bool native_resource_registered{};
  std::uint64_t video_frames{};
  std::uint64_t audio_sample_frames{};
  std::int64_t timeline_origin_us{-1};
  std::uint32_t video_sample_size{};

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device12;
  ComPtr<ID3D12CommandQueue> queue12;
  ComPtr<ID3D11Device> device11;
  ComPtr<IMFDXGIDeviceManager> device_manager;
  UINT device_manager_token{};

  std::unique_ptr<WindowsD3D12P010Dispatch> dispatch;
  std::unique_ptr<WindowsD3D12P010Converter> converter;
  std::shared_ptr<ProductionAudioMediaPipeline> audio_pipeline;

  ComPtr<IMFSinkWriter> writer;
  DWORD video_stream{};
  DWORD audio_stream{};
  std::uint32_t audio_sample_rate{};
  std::uint32_t audio_channels{};

  void release_audio_revision() noexcept {
    if (audio_revision_locked && audio_pipeline) {
      audio_pipeline->end_export_revision();
      audio_revision_locked = false;
    }
  }

  ~State() {
    writer.Reset();
    if (mf_started) {
      MFShutdown();
      mf_started = false;
    }
    release_audio_revision();
    if (!finalized && !staged_path.empty()) {
      std::error_code error;
      std::filesystem::remove(staged_path, error);
    }
  }
};

bool utf8_to_wide(const std::string& value, std::wstring& output) {
  output.clear();
  if (value.empty()) return false;
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
  if (required <= 0) return false;
  output.assign(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
                          output.data(), required) <= 0) {
    output.clear();
    return false;
  }
  if (!output.empty() && output.back() == L'\0') output.pop_back();
  return !output.empty();
}

std::filesystem::path staged_output_path(const std::filesystem::path& final_path) {
  static std::atomic<std::uint64_t> sequence{0};
  const auto tag = std::to_wstring(static_cast<unsigned long long>(GetCurrentProcessId())) +
                   L"-" +
                   std::to_wstring(static_cast<unsigned long long>(GetTickCount64())) +
                   L"-" +
                   std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
  const auto stem =
      final_path.stem().wstring() + L".digitor-partial-" + tag;
  return final_path.parent_path() / (stem + final_path.extension().wstring());
}

DigitorResult open_same_adapter_devices(
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

    DXGI_ADAPTER_DESC1 adapter_desc{};
    adapter->GetDesc1(&adapter_desc);
    if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

    ComPtr<ID3D12Device> device12;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device12)))) {
      continue;
    }

    ComPtr<ID3D12Resource> probe;
    if (FAILED(device12->OpenSharedHandle(shared, IID_PPV_ARGS(&probe)))) continue;
    const auto desc = probe->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Width != width || desc.Height != height ||
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
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

DigitorResult configure_audio_stream(State& state, std::string& diagnostic) {
  if (!state.audio_pipeline) {
    state.audio_enabled = false;
    return DIGITOR_RESULT_OK;
  }
  state.audio_sample_rate = state.audio_pipeline->sample_rate();
  state.audio_channels = state.audio_pipeline->channels();
  if (!state.audio_sample_rate || !state.audio_channels) {
    diagnostic = "canonical audio pipeline has no negotiated format";
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  if (state.audio_channels > 2) {
    diagnostic =
        "Windows production AAC sink currently supports mono/stereo canonical audio";
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

  const std::uint64_t block_alignment =
      static_cast<std::uint64_t>(state.audio_channels) * sizeof(std::int16_t);
  const std::uint64_t bytes_per_second =
      static_cast<std::uint64_t>(state.audio_sample_rate) * block_alignment;
  if (block_alignment > (std::numeric_limits<UINT32>::max)() ||
      bytes_per_second > (std::numeric_limits<UINT32>::max)()) {
    diagnostic = "canonical audio PCM format exceeds Media Foundation limits";
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
      FAILED(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                              static_cast<UINT32>(bytes_per_second))) ||
      FAILED(input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE)) ||
      FAILED(state.writer->SetInputMediaType(state.audio_stream, input.Get(),
                                             nullptr))) {
    diagnostic = "failed to configure canonical PCM audio input";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  state.audio_enabled = true;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

DigitorResult initialize_writer(
    State& state, const DigitorNativeGpuTextureDescriptor& descriptor,
    std::string& diagnostic) {
  if (state.initialized) return DIGITOR_RESULT_OK;

  auto result = open_same_adapter_devices(
      state, reinterpret_cast<HANDLE>(descriptor.native_handle),
      descriptor.width, descriptor.height, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;

  const bool ten_bit_video =
      state.config.profile.ten_bit || state.snapshot->data().hdr;
  if (state.config.profile.codec == ExportCodec::h264 && ten_bit_video) {
    diagnostic = "Windows Media Foundation H.264 export requires 8-bit NV12 input";
    return DIGITOR_RESULT_UNSUPPORTED;
  }

  WindowsD3D12P010DispatchConfig dispatch_config{};
  dispatch_config.device = state.device12.Get();
  dispatch_config.input_format = DIGITOR_PIXEL_FORMAT_RGBA8_UNORM;
  dispatch_config.source_starts_shader_readable = false;
  state.dispatch =
      std::make_unique<WindowsD3D12P010Dispatch>(std::move(dispatch_config));
  result = state.dispatch->initialize();
  if (result != DIGITOR_RESULT_OK) {
    diagnostic = "embedded D3D12 RGBA-to-YUV dispatch initialization failed";
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
  conversion.ten_bit_output = ten_bit_video;
  conversion.gpu_dispatch = state.dispatch->callback();
  state.converter =
      std::make_unique<WindowsD3D12P010Converter>(std::move(conversion));
  result = state.converter->initialize();
  if (result != DIGITOR_RESULT_OK) {
    diagnostic = "shared D3D12/D3D11 encoder YUV converter initialization failed";
    return result;
  }

  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation startup failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  state.mf_started = true;

  const auto output_wide = state.staged_path.wstring();
  ComPtr<IMFAttributes> attributes;
  hr = MFCreateAttributes(&attributes, 4);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation sink attributes creation failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE,
                           MFTranscodeContainerType_MPEG4);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation MP4 container attribute failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation hardware transform attribute failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  hr = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation sink throttling attribute failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  hr = attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,
                              state.device_manager.Get());
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation D3D manager attribute failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  hr = MFCreateSinkWriterFromURL(output_wide.c_str(), nullptr,
                                 attributes.Get(), &state.writer);
  if (FAILED(hr) || !state.writer) {
    char message[256]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation MP4 sink writer creation failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<IMFMediaType> video_output;
  if (FAILED(MFCreateMediaType(&video_output)) ||
      FAILED(video_output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(video_output->SetGUID(
          MF_MT_SUBTYPE, state.config.profile.codec == ExportCodec::hevc
                             ? MFVideoFormat_HEVC
                             : MFVideoFormat_H264)) ||
      state.config.profile.video_bitrate <= 0 ||
      state.config.profile.video_bitrate >
          static_cast<std::int64_t>((std::numeric_limits<UINT32>::max)()) ||
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
  const GUID input_subtype =
      ten_bit_video ? MFVideoFormat_P010 : MFVideoFormat_NV12;
  UINT32 sample_size = 0;
  const HRESULT sample_size_hr = MFCalculateImageSize(
      input_subtype, descriptor.width, descriptor.height, &sample_size);
  if (FAILED(sample_size_hr) || sample_size == 0) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation could not calculate encoder input sample size: HRESULT=0x%08lX",
                  static_cast<unsigned long>(
                      static_cast<std::uint32_t>(sample_size_hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  state.video_sample_size = sample_size;

  if (FAILED(MFCreateMediaType(&video_input)) ||
      FAILED(video_input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
      FAILED(video_input->SetGUID(MF_MT_SUBTYPE, input_subtype)) ||
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
      FAILED(video_input->SetUINT32(MF_MT_SAMPLE_SIZE, sample_size)) ||
      FAILED(video_input->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE)) ||
      FAILED(video_input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE)) ||
      FAILED(state.writer->SetInputMediaType(state.video_stream,
                                             video_input.Get(), nullptr))) {
    diagnostic = ten_bit_video
                     ? "failed to configure complete P010 hardware video input"
                     : "failed to configure complete NV12 hardware video input";
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

DigitorResult write_audio_block(State& state, ConstAudioBufferView source,
                                std::int64_t timeline_start_us,
                                std::string& diagnostic) {
  if (!state.writer || !state.audio_enabled || !state.audio_sample_rate ||
      !state.audio_channels || !source.channels ||
      source.channel_count != state.audio_channels || !source.frame_count ||
      timeline_start_us < state.timeline_origin_us) {
    diagnostic = "invalid canonical audio block for Media Foundation";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  for (std::uint32_t channel = 0; channel < source.channel_count; ++channel) {
    if (!source.channels[channel]) {
      diagnostic = "canonical audio block contains a null channel";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }

  const std::uint64_t sample_count =
      static_cast<std::uint64_t>(source.frame_count) * source.channel_count;
  if (sample_count >
      (std::numeric_limits<DWORD>::max)() / sizeof(std::int16_t)) {
    diagnostic = "canonical audio block exceeds Media Foundation buffer limit";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  const auto bytes = static_cast<DWORD>(sample_count * sizeof(std::int16_t));

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
  for (std::uint32_t frame = 0; frame < source.frame_count; ++frame) {
    for (std::uint32_t channel = 0; channel < source.channel_count; ++channel) {
      const auto value = std::clamp(source.channels[channel][frame], -1.0f, 1.0f);
      pcm[static_cast<std::size_t>(frame) * source.channel_count + channel] =
          static_cast<std::int16_t>(std::lrint(value * 32767.0f));
    }
  }
  buffer->Unlock();
  if (FAILED(buffer->SetCurrentLength(bytes))) {
    diagnostic = "Media Foundation audio buffer length failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<IMFSample> sample;
  if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get()))) {
    diagnostic = "Media Foundation audio sample creation failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  const auto normalized_us = timeline_start_us - state.timeline_origin_us;
  if (normalized_us > (std::numeric_limits<LONGLONG>::max)() / 10) {
    diagnostic = "canonical audio timestamp exceeds Media Foundation range";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  const auto duration_100ns = static_cast<LONGLONG>(
      (static_cast<std::uint64_t>(source.frame_count) * 10'000'000ULL) /
      state.audio_sample_rate);
  if (duration_100ns <= 0 ||
      FAILED(sample->SetSampleTime(normalized_us * 10)) ||
      FAILED(sample->SetSampleDuration(duration_100ns)) ||
      FAILED(state.writer->WriteSample(state.audio_stream, sample.Get()))) {
    diagnostic = "Media Foundation rejected canonical synchronized audio";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  state.audio_sample_frames += source.frame_count;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

DigitorResult write_audio_until(State& state, std::int64_t target_us,
                                std::string& diagnostic) {
  if (!state.audio_enabled) return DIGITOR_RESULT_OK;
  if (!state.audio_pipeline || !state.audio_revision_locked ||
      state.timeline_origin_us < 0 || state.config.duration_us <= 0) {
    diagnostic = "canonical export audio pipeline is not initialized";
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  target_us = (std::min)(target_us, state.config.duration_us);
  if (target_us <= 0) return DIGITOR_RESULT_OK;

  while (true) {
    const auto cursor_us = static_cast<std::int64_t>(
        (static_cast<long double>(state.audio_sample_frames) * 1'000'000.0L) /
        static_cast<long double>(state.audio_sample_rate));
    if (cursor_us >= target_us) break;
    const auto remaining_us = target_us - cursor_us;
    auto requested_frames = static_cast<std::uint64_t>(std::ceil(
        (static_cast<long double>(remaining_us) * state.audio_sample_rate) /
        1'000'000.0L));
    requested_frames = (std::max<std::uint64_t>)(requested_frames, 1);
    requested_frames = (std::min<std::uint64_t>)(
        requested_frames, ProductionAudioMediaPipeline::maximum_block_frames());

    if (state.timeline_origin_us >
        (std::numeric_limits<std::int64_t>::max)() - cursor_us) {
      diagnostic = "canonical audio timeline timestamp overflow";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    const auto timeline_start = state.timeline_origin_us + cursor_us;
    bool had_source_audio = false;
    auto render_result = state.audio_pipeline->render_export(
        timeline_start, static_cast<std::uint32_t>(requested_frames),
        [&state](ConstAudioBufferView source, std::int64_t block_start,
                 std::string& sink_diagnostic) {
          return write_audio_block(state, source, block_start, sink_diagnostic);
        },
        &had_source_audio, &diagnostic);
    if (render_result != DIGITOR_RESULT_OK) return render_result;
    state.audio_source_rendered =
        state.audio_source_rendered || had_source_audio;
  }

  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

DigitorResult write_video_sample(
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
    diagnostic = "GPU RGBA-to-encoder-YUV conversion failed";
    return result;
  }
  if (!surface.resource || surface.width != descriptor.width ||
      surface.height != descriptor.height ||
      surface.timestamp_us != input.pts_us ||
      surface.frame_identity != input.frame->identity()) {
    diagnostic = "encoder YUV surface identity/timestamp differs from final GPU frame";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }

  const bool ten_bit_video =
      state.config.profile.ten_bit || state.snapshot->data().hdr;
  const DXGI_FORMAT expected_format =
      ten_bit_video ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
  auto* texture = static_cast<ID3D11Texture2D*>(surface.resource);
  D3D11_TEXTURE2D_DESC texture_desc{};
  texture->GetDesc(&texture_desc);
  if (texture_desc.Format != expected_format ||
      texture_desc.Width != descriptor.width ||
      texture_desc.Height != descriptor.height) {
    diagnostic = ten_bit_video
                     ? "encoder surface is not the required P010 GPU texture"
                     : "encoder surface is not the required NV12 GPU texture";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }

  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0,
                                         FALSE, &buffer);
  if (FAILED(hr) || !buffer) {
    char message[224]{};
    std::snprintf(
        message, sizeof(message),
        "%s: HRESULT=0x%08lX",
        ten_bit_video ? "Media Foundation could not wrap P010 GPU surface"
                      : "Media Foundation could not wrap NV12 GPU surface",
        static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  if (state.video_sample_size == 0) {
    diagnostic = "Media Foundation encoder input sample size was not initialized";
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }

  DWORD maximum_length = 0;
  hr = buffer->GetMaxLength(&maximum_length);
  if (FAILED(hr)) {
    char message[224]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation GPU buffer GetMaxLength failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (maximum_length < state.video_sample_size) {
    char message[224]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation GPU buffer is smaller than the negotiated frame: max=%lu required=%u",
                  static_cast<unsigned long>(maximum_length),
                  state.video_sample_size);
    diagnostic = message;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  hr = buffer->SetCurrentLength(static_cast<DWORD>(state.video_sample_size));
  if (FAILED(hr)) {
    char message[224]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation GPU buffer SetCurrentLength(%u) failed: HRESULT=0x%08lX",
                  state.video_sample_size,
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  DWORD current_length = 0;
  hr = buffer->GetCurrentLength(&current_length);
  if (FAILED(hr) || current_length != state.video_sample_size) {
    char message[224]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation GPU buffer valid length mismatch: current=%lu required=%u HRESULT=0x%08lX",
                  static_cast<unsigned long>(current_length),
                  state.video_sample_size,
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  if (FAILED(hr) || !sample) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation video sample creation failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  hr = sample->AddBuffer(buffer.Get());
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation video sample AddBuffer failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  if (state.timeline_origin_us < 0) state.timeline_origin_us = input.pts_us;
  if (state.timeline_origin_us < 0 || input.pts_us < state.timeline_origin_us ||
      input.duration_us <= 0) {
    diagnostic = "video timestamp is outside the unified export timeline";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  const auto normalized_pts = input.pts_us - state.timeline_origin_us;
  if (normalized_pts > (std::numeric_limits<LONGLONG>::max)() / 10 ||
      input.duration_us > (std::numeric_limits<LONGLONG>::max)() / 10) {
    diagnostic = "video timestamp exceeds Media Foundation range";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  hr = sample->SetSampleTime(normalized_pts * 10);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation SetSampleTime failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  hr = sample->SetSampleDuration(input.duration_us * 10);
  if (FAILED(hr)) {
    char message[192]{};
    std::snprintf(message, sizeof(message),
                  "Media Foundation SetSampleDuration failed: HRESULT=0x%08lX",
                  static_cast<unsigned long>(static_cast<std::uint32_t>(hr)));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  hr = state.writer->WriteSample(state.video_stream, sample.Get());
  if (FAILED(hr)) {
    char message[256]{};
    std::snprintf(
        message, sizeof(message),
        "Media Foundation WriteSample rejected final graded GPU video sample: HRESULT=0x%08lX validBytes=%lu maxBytes=%lu",
        static_cast<unsigned long>(static_cast<std::uint32_t>(hr)),
        static_cast<unsigned long>(current_length),
        static_cast<unsigned long>(maximum_length));
    diagnostic = message;
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ++state.video_frames;
  state.synchronization_waited = true;
  state.native_resource_registered = true;

  if (normalized_pts >
      (std::numeric_limits<std::int64_t>::max)() - input.duration_us) {
    diagnostic = "video end timestamp overflow";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  result = write_audio_until(state, normalized_pts + input.duration_us, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;

  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

void abort_writer(State& state) noexcept {
  state.writer.Reset();
  if (state.mf_started) {
    MFShutdown();
    state.mf_started = false;
  }
  state.initialized = false;
}

ProductionEncoderFactoryResult create_windows_unified_export_encoder(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    std::string source_media_path,
    TextureDescriptorBuilder descriptor_builder) {
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
    result.diagnostic = "unified Windows exporter supports H.264 and HEVC";
    return result;
  }
  if (data.profile.codec == ExportCodec::h264 &&
      (data.profile.ten_bit || data.hdr)) {
    result.diagnostic =
        "Windows Media Foundation H.264 export does not accept the required 10-bit P010 input";
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

  auto acquired =
      acquire_production_audio_media_pipeline(state->source_media_path);
  if (acquired.result != DIGITOR_RESULT_OK) {
    result.diagnostic = acquired.diagnostic.empty()
                            ? "canonical production audio pipeline unavailable"
                            : acquired.diagnostic;
    return result;
  }
  if (!acquired.no_audio_stream) {
    if (!acquired.pipeline) {
      result.diagnostic = "canonical production audio pipeline is missing";
      return result;
    }
    std::string audio_diagnostic;
    const auto lock_result = acquired.pipeline->begin_export_revision(
        data.audio_revision, &audio_diagnostic);
    if (lock_result != DIGITOR_RESULT_OK) {
      result.diagnostic = audio_diagnostic.empty()
                              ? "audio revision could not be frozen for export"
                              : audio_diagnostic;
      return result;
    }
    state->audio_pipeline = std::move(acquired.pipeline);
    state->audio_revision_locked = true;
  }

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
              !config.require_atomic_finalize || config.output_path.empty() ||
              config.duration_us <= 0) {
            diagnostic =
                "unified Windows export requires hardware zero-copy atomic mode";
            return DIGITOR_RESULT_UNSUPPORTED;
          }

          std::wstring final_wide;
          if (!utf8_to_wide(config.output_path, final_wide)) {
            diagnostic = "Windows export path is not valid UTF-8";
            return DIGITOR_RESULT_INVALID_ARGUMENT;
          }
          state->config = config;
          state->final_path = std::filesystem::path(final_wide);
          state->staged_path = staged_output_path(state->final_path);
          std::error_code error;
          if (!state->final_path.parent_path().empty()) {
            std::filesystem::create_directories(
                state->final_path.parent_path(), error);
            if (error) {
              diagnostic = "failed to create Windows export directory: " +
                           error.message();
              return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            }
          }
          error.clear();
          std::filesystem::remove(state->staged_path, error);
          if (error) {
            diagnostic = "failed to clear Windows export staging file: " +
                         error.message();
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }
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
              metadata.width != frozen.width || metadata.height != frozen.height ||
              metadata.color_metadata != frozen.color_metadata ||
              (metadata.format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT &&
               metadata.format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)) {
            diagnostic =
                "final graded GPU frame differs from frozen export contract";
            return DIGITOR_RESULT_INVALID_ARGUMENT;
          }

          DigitorNativeGpuTextureDescriptor descriptor{};
          descriptor.struct_size = sizeof(descriptor);
          descriptor.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
          const auto generation =
              input.frame->identity() ? input.frame->identity() : 1;
          auto encode_result = state->descriptor_builder(
              input.frame, generation, descriptor, diagnostic);
          if (encode_result != DIGITOR_RESULT_OK) return encode_result;

          if (descriptor.backend != DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12 ||
              descriptor.handle_type !=
                  DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE ||
              descriptor.pixel_format != DIGITOR_PIXEL_FORMAT_RGBA8_UNORM ||
              !descriptor.native_handle || descriptor.width != frozen.width ||
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

          return write_video_sample(*state, input, descriptor, diagnostic);
        } catch (const std::bad_alloc&) {
          diagnostic = "out of memory encoding final Windows GPU frame";
          return DIGITOR_RESULT_OUTOFMEMORY;
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

          auto finish_result =
              write_audio_until(*state, state->config.duration_us, diagnostic);
          if (finish_result != DIGITOR_RESULT_OK) {
            abort_writer(*state);
            state->release_audio_revision();
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
            state->release_audio_revision();
            diagnostic = "Media Foundation A/V export finalization failed";
            std::error_code error;
            std::filesystem::remove(state->staged_path, error);
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }

          if (!MoveFileExW(state->staged_path.c_str(), state->final_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            state->release_audio_revision();
            diagnostic =
                "failed to atomically publish synchronized Windows export";
            std::error_code error;
            std::filesystem::remove(state->staged_path, error);
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
          }

          state->finalized = true;
          state->release_audio_revision();
          diagnostic.clear();
          return DIGITOR_RESULT_OK;
        } catch (const std::bad_alloc&) {
          state->release_audio_revision();
          diagnostic = "out of memory finalizing unified Windows export";
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        } catch (...) {
          state->release_audio_revision();
          diagnostic = "unexpected unified Windows export finalization failure";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
      };

  result.callbacks.cancel = [state]() noexcept {
    std::scoped_lock lock(state->mutex);
    if (state->cancelled) return;
    state->cancelled = true;
    abort_writer(*state);
    state->release_audio_revision();
    std::error_code error;
    std::filesystem::remove(state->staged_path, error);
  };

  result.zero_copy_qualified = [state]() {
    std::scoped_lock lock(state->mutex);
    const bool audio_ok =
        !state->audio_enabled ||
        (state->audio_sample_frames > 0 && state->audio_source_rendered);
    return state->finalized && state->video_frames > 0 &&
           state->no_cpu_pixels && state->synchronization_waited &&
           state->native_resource_registered && audio_ok;
  };
  result.windows_vulkan_interop_qualified = false;
  result.diagnostic.clear();
  return result;
}

}  // namespace

}  // namespace windows_unified_export_detail

ProductionEncoderFactory make_windows_unified_export_factory(
    windows_unified_export_detail::TextureDescriptorBuilder descriptor_builder,
    std::function<std::string()> source_media_path_getter) {
  if (!descriptor_builder || !source_media_path_getter) return {};
  return [descriptor_builder = std::move(descriptor_builder),
          source_media_path_getter = std::move(source_media_path_getter)](
             std::shared_ptr<const ExportRenderSnapshot> snapshot) {
    return windows_unified_export_detail::create_windows_unified_export_encoder(
        std::move(snapshot), source_media_path_getter(), descriptor_builder);
  };
}

}  // namespace digitor

#endif  // defined(_WIN32)
