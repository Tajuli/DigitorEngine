#include "digitor/audio_latency_probe.hpp"

#include <limits>

#if defined(_WIN32)
#define NOMINMAX
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <wrl/client.h>
#elif defined(__ANDROID__)
#include <aaudio/AAudio.h>
#elif defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#endif

namespace digitor {
namespace {

std::int64_t frames_to_us(std::uint64_t frames, std::uint32_t sample_rate) noexcept {
  if (sample_rate == 0) return 0;
  constexpr std::uint64_t million = 1000000ULL;
  if (frames > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / million) return 0;
  return static_cast<std::int64_t>((frames * million) / sample_rate);
}

#if defined(_WIN32)
AudioLatencyProbeResult probe_wasapi() noexcept {
  AudioLatencyProbeResult result;
  result.backend = AudioLatencyBackend::wasapi;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize = SUCCEEDED(initialized);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    result.diagnostic = "CoInitializeEx failed";
    return result;
  }

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  Microsoft::WRL::ComPtr<IMMDevice> device;
  Microsoft::WRL::ComPtr<IAudioClient> client;
  WAVEFORMATEX* format = nullptr;
  REFERENCE_TIME default_period = 0;
  REFERENCE_TIME minimum_period = 0;
  UINT32 buffer_frames = 0;

  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(client.GetAddressOf()));
  if (SUCCEEDED(hr)) hr = client->GetMixFormat(&format);
  if (SUCCEEDED(hr)) hr = client->GetDevicePeriod(&default_period, &minimum_period);
  if (SUCCEEDED(hr)) {
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, default_period, 0, format, nullptr);
  }
  if (SUCCEEDED(hr)) hr = client->GetBufferSize(&buffer_frames);

  if (SUCCEEDED(hr) && format != nullptr && format->nSamplesPerSec > 0) {
    result.available = true;
    result.sample_rate = format->nSamplesPerSec;
    result.buffer_frames = buffer_frames;
    result.device_latency_us = default_period / 10;
    result.buffer_latency_us = frames_to_us(buffer_frames, result.sample_rate);
    result.total_latency_us = result.device_latency_us + result.buffer_latency_us;
    result.diagnostic = "WASAPI shared-mode default output";
  } else {
    result.diagnostic = "WASAPI default output unavailable";
  }

  if (format != nullptr) CoTaskMemFree(format);
  if (uninitialize) CoUninitialize();
  return result;
}
#elif defined(__ANDROID__)
AudioLatencyProbeResult probe_aaudio() noexcept {
  AudioLatencyProbeResult result;
  result.backend = AudioLatencyBackend::aaudio;
  AAudioStreamBuilder* builder = nullptr;
  AAudioStream* stream = nullptr;
  aaudio_result_t status = AAudio_createStreamBuilder(&builder);
  if (status == AAUDIO_OK) {
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    status = AAudioStreamBuilder_openStream(builder, &stream);
  }
  if (builder != nullptr) AAudioStreamBuilder_delete(builder);

  if (status == AAUDIO_OK && stream != nullptr) {
    const int32_t sample_rate = AAudioStream_getSampleRate(stream);
    const int32_t burst = AAudioStream_getFramesPerBurst(stream);
    const int32_t capacity = AAudioStream_getBufferCapacityInFrames(stream);
    if (sample_rate > 0 && capacity > 0) {
      result.available = true;
      result.sample_rate = static_cast<std::uint32_t>(sample_rate);
      result.buffer_frames = static_cast<std::uint32_t>(capacity);
      result.safety_frames = burst > 0 ? static_cast<std::uint32_t>(burst) : 0U;
      result.device_latency_us = frames_to_us(result.safety_frames, result.sample_rate);
      result.buffer_latency_us = frames_to_us(result.buffer_frames, result.sample_rate);
      result.total_latency_us = result.device_latency_us + result.buffer_latency_us;
      result.diagnostic = "AAudio low-latency shared output";
    }
    AAudioStream_close(stream);
  } else {
    result.diagnostic = "AAudio default output unavailable";
  }
  return result;
}
#elif defined(__APPLE__)
AudioLatencyProbeResult probe_core_audio() noexcept {
  AudioLatencyProbeResult result;
  result.backend = AudioLatencyBackend::core_audio;
  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain};
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device);
  if (status != noErr || device == kAudioObjectUnknown) {
    result.diagnostic = "CoreAudio default output unavailable";
    return result;
  }

  Float64 sample_rate = 0.0;
  UInt32 buffer_frames = 0;
  UInt32 device_latency = 0;
  UInt32 safety_offset = 0;

  address = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
             kAudioObjectPropertyElementMain};
  size = sizeof(sample_rate);
  status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &sample_rate);
  if (status == noErr) {
    address = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
               kAudioObjectPropertyElementMain};
    size = sizeof(buffer_frames);
    status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &buffer_frames);
  }
  if (status == noErr) {
    address = {kAudioDevicePropertyLatency, kAudioDevicePropertyScopeOutput,
               kAudioObjectPropertyElementMain};
    size = sizeof(device_latency);
    status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &device_latency);
  }
  if (status == noErr) {
    address = {kAudioDevicePropertySafetyOffset, kAudioDevicePropertyScopeOutput,
               kAudioObjectPropertyElementMain};
    size = sizeof(safety_offset);
    status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &safety_offset);
  }

  if (status == noErr && sample_rate > 0.0 && buffer_frames > 0) {
    result.available = true;
    result.sample_rate = static_cast<std::uint32_t>(sample_rate + 0.5);
    result.buffer_frames = buffer_frames;
    result.safety_frames = device_latency + safety_offset;
    result.device_latency_us = frames_to_us(result.safety_frames, result.sample_rate);
    result.buffer_latency_us = frames_to_us(result.buffer_frames, result.sample_rate);
    result.total_latency_us = result.device_latency_us + result.buffer_latency_us;
    result.diagnostic = "CoreAudio default output device";
  } else {
    result.diagnostic = "CoreAudio output properties unavailable";
  }
  return result;
}
#endif

}  // namespace

AudioLatencyProbeResult probe_default_audio_output() noexcept {
#if defined(_WIN32)
  return probe_wasapi();
#elif defined(__ANDROID__)
  return probe_aaudio();
#elif defined(__APPLE__)
  return probe_core_audio();
#else
  AudioLatencyProbeResult result;
  result.diagnostic = "No native audio latency adapter for this platform";
  return result;
#endif
}

const char* audio_latency_backend_name(AudioLatencyBackend backend) noexcept {
  switch (backend) {
    case AudioLatencyBackend::wasapi: return "WASAPI";
    case AudioLatencyBackend::aaudio: return "AAudio";
    case AudioLatencyBackend::core_audio: return "CoreAudio";
    case AudioLatencyBackend::unavailable: return "Unavailable";
  }
  return "Unavailable";
}

}  // namespace digitor
