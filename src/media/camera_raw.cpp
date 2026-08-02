#include "digitor/camera_raw.hpp"

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace digitor {
namespace {

void set_diagnostic(std::string* diagnostic, std::string value) {
  if (diagnostic) *diagnostic = std::move(value);
}

bool supports(const RawAdapterCapabilities& capabilities, RawCodec codec) {
  return std::find(capabilities.codecs.begin(), capabilities.codecs.end(), codec) !=
         capabilities.codecs.end();
}

std::string cache_key(const RawDecodeRequest& request) {
  std::ostringstream key;
  const auto& s = request.settings;
  key << request.frame_index << '|' << request.timestamp_seconds << '|'
      << static_cast<int>(s.quality) << '|' << static_cast<int>(s.white_balance)
      << '|' << static_cast<int>(s.output_encoding) << '|' << s.temperature_kelvin
      << '|' << s.tint << '|' << s.iso << '|' << s.exposure_stops << '|'
      << s.highlight_recovery << '|' << s.shadow_recovery << '|' << s.black_level
      << '|' << s.saturation << '|' << s.use_camera_metadata << '|'
      << s.prefer_gpu << '|' << s.require_gpu << '|' << s.preserve_negative_values;
  return key.str();
}

}  // namespace

struct CameraRawEngine::Impl {
  explicit Impl(std::size_t capacity) : cache_capacity(std::max<std::size_t>(1, capacity)) {}

  struct CacheEntry {
    RawDecodedFrame frame;
    std::list<std::string>::iterator lru;
  };

  mutable std::mutex mutex;
  std::vector<RawAdapterCallbacks> adapters;
  std::size_t active_adapter{static_cast<std::size_t>(-1)};
  std::shared_ptr<void> session;
  std::string source_path;
  RawClipMetadata metadata;
  CameraRawTelemetry telemetry;
  std::size_t cache_capacity;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> lru;

  void clear_cache() {
    cache.clear();
    lru.clear();
  }
};

CameraRawEngine::CameraRawEngine(std::size_t frame_cache_capacity)
    : impl_(std::make_unique<Impl>(frame_cache_capacity)) {}
CameraRawEngine::~CameraRawEngine() { close(); }
CameraRawEngine::CameraRawEngine(CameraRawEngine&&) noexcept = default;
CameraRawEngine& CameraRawEngine::operator=(CameraRawEngine&&) noexcept = default;

DigitorResult CameraRawEngine::register_adapter(RawAdapterCallbacks adapter,
                                                std::string* diagnostic) {
  std::scoped_lock lock(impl_->mutex);
  if (adapter.capabilities.name.empty() || adapter.capabilities.codecs.empty() ||
      !adapter.probe || !adapter.open || !adapter.decode || !adapter.close) {
    impl_->telemetry.last_error = "RAW adapter requires a name, codecs, probe, open, decode and close callbacks";
    set_diagnostic(diagnostic, impl_->telemetry.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  const auto duplicate = std::find_if(impl_->adapters.begin(), impl_->adapters.end(),
                                      [&](const auto& value) {
                                        return value.capabilities.name == adapter.capabilities.name;
                                      });
  if (duplicate != impl_->adapters.end()) {
    impl_->telemetry.last_error = "RAW adapter name is already registered";
    set_diagnostic(diagnostic, impl_->telemetry.last_error);
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  impl_->adapters.push_back(std::move(adapter));
  impl_->telemetry.registered_adapters = impl_->adapters.size();
  impl_->telemetry.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult CameraRawEngine::probe(const std::string& source_path,
                                     RawClipMetadata& metadata,
                                     std::string* diagnostic) {
  std::scoped_lock lock(impl_->mutex);
  if (source_path.empty()) {
    set_diagnostic(diagnostic, "RAW source path must not be empty");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  ++impl_->telemetry.probes;
  std::string last_error = "no registered RAW adapter accepted the source";
  for (const auto& adapter : impl_->adapters) {
    RawClipMetadata candidate;
    std::string error;
    const auto result = adapter.probe(source_path, candidate, error);
    if (result != DIGITOR_RESULT_OK) {
      if (!error.empty()) last_error = error;
      continue;
    }
    if (candidate.codec == RawCodec::unknown || !supports(adapter.capabilities, candidate.codec) ||
        candidate.width == 0 || candidate.height == 0 || candidate.bit_depth < 10 ||
        !std::isfinite(candidate.frame_rate) || candidate.frame_rate <= 0.0) {
      last_error = "RAW adapter returned incomplete or inconsistent clip metadata";
      ++impl_->telemetry.adapter_failures;
      continue;
    }
    metadata = std::move(candidate);
    impl_->telemetry.last_error.clear();
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }
  impl_->telemetry.last_error = last_error;
  set_diagnostic(diagnostic, last_error);
  return DIGITOR_RESULT_UNSUPPORTED;
}

DigitorResult CameraRawEngine::open(const std::string& source_path,
                                    std::string* diagnostic) {
  std::scoped_lock lock(impl_->mutex);
  if (source_path.empty()) {
    set_diagnostic(diagnostic, "RAW source path must not be empty");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (impl_->session) {
    set_diagnostic(diagnostic, "a RAW clip is already open");
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  std::string last_error = "no registered RAW adapter could open the source";
  for (std::size_t i = 0; i < impl_->adapters.size(); ++i) {
    auto& adapter = impl_->adapters[i];
    RawClipMetadata metadata;
    std::string error;
    if (adapter.probe(source_path, metadata, error) != DIGITOR_RESULT_OK ||
        !supports(adapter.capabilities, metadata.codec)) {
      if (!error.empty()) last_error = error;
      continue;
    }
    std::shared_ptr<void> session;
    const auto result = adapter.open(source_path, session, error);
    if (result != DIGITOR_RESULT_OK || !session) {
      ++impl_->telemetry.adapter_failures;
      if (!error.empty()) last_error = error;
      continue;
    }
    impl_->active_adapter = i;
    impl_->session = std::move(session);
    impl_->source_path = source_path;
    impl_->metadata = std::move(metadata);
    impl_->clear_cache();
    ++impl_->telemetry.opens;
    impl_->telemetry.active_adapter = adapter.capabilities.name;
    impl_->telemetry.last_error.clear();
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }
  impl_->telemetry.last_error = last_error;
  set_diagnostic(diagnostic, last_error);
  return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

DigitorResult CameraRawEngine::decode(const RawDecodeRequest& request,
                                      RawDecodedFrame& frame,
                                      std::string* diagnostic) {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->session || impl_->active_adapter >= impl_->adapters.size()) {
    set_diagnostic(diagnostic, "no RAW clip is open");
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  if (!request.source_path.empty() && request.source_path != impl_->source_path) {
    set_diagnostic(diagnostic, "decode request does not match the open RAW source");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (validate_settings(request.settings, diagnostic) != DIGITOR_RESULT_OK) {
    ++impl_->telemetry.rejected_requests;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (impl_->metadata.frame_count != 0 && request.frame_index >= impl_->metadata.frame_count) {
    ++impl_->telemetry.rejected_requests;
    set_diagnostic(diagnostic, "RAW frame index is outside the clip");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  const std::string key = cache_key(request);
  if (auto found = impl_->cache.find(key); found != impl_->cache.end()) {
    impl_->lru.splice(impl_->lru.begin(), impl_->lru, found->second.lru);
    frame = found->second.frame;
    ++impl_->telemetry.cache_hits;
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }

  auto& adapter = impl_->adapters[impl_->active_adapter];
  if (request.settings.require_gpu && !adapter.capabilities.gpu_decode) {
    ++impl_->telemetry.rejected_requests;
    set_diagnostic(diagnostic, "the active RAW adapter cannot satisfy require_gpu");
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  RawDecodedFrame decoded;
  std::string error;
  const auto result = adapter.decode(impl_->session, request, decoded, error);
  if (result != DIGITOR_RESULT_OK) {
    ++impl_->telemetry.adapter_failures;
    impl_->telemetry.last_error = error;
    set_diagnostic(diagnostic, error);
    return result;
  }
  const bool valid_gpu = decoded.gpu_resident && decoded.native_surface &&
                         decoded.backend != DIGITOR_RENDERER_CPU;
  const bool valid_cpu = !decoded.gpu_resident && !decoded.rgba32f.empty() &&
                         decoded.backend == DIGITOR_RENDERER_CPU;
  if (decoded.width == 0 || decoded.height == 0 || (!valid_gpu && !valid_cpu) ||
      (request.settings.require_gpu && !valid_gpu)) {
    ++impl_->telemetry.adapter_failures;
    impl_->telemetry.last_error = "RAW adapter returned an invalid or policy-incompatible frame";
    set_diagnostic(diagnostic, impl_->telemetry.last_error);
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  if (valid_cpu && decoded.rgba32f.size() !=
                       static_cast<std::size_t>(decoded.width) * decoded.height * 4u) {
    ++impl_->telemetry.adapter_failures;
    set_diagnostic(diagnostic, "RAW CPU frame has an invalid RGBA32F payload size");
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }

  decoded.frame_index = request.frame_index;
  decoded.timestamp_seconds = request.timestamp_seconds;
  frame = decoded;
  impl_->lru.push_front(key);
  impl_->cache.emplace(key, Impl::CacheEntry{decoded, impl_->lru.begin()});
  while (impl_->cache.size() > impl_->cache_capacity) {
    const auto victim = impl_->lru.back();
    impl_->lru.pop_back();
    impl_->cache.erase(victim);
  }
  ++impl_->telemetry.decoded_frames;
  if (valid_gpu) ++impl_->telemetry.gpu_frames;
  else ++impl_->telemetry.cpu_frames;
  impl_->telemetry.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult CameraRawEngine::seek(std::uint64_t frame_index,
                                    std::string* diagnostic) {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->session || impl_->active_adapter >= impl_->adapters.size()) {
    set_diagnostic(diagnostic, "no RAW clip is open");
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  if (impl_->metadata.frame_count != 0 && frame_index >= impl_->metadata.frame_count) {
    set_diagnostic(diagnostic, "RAW seek frame is outside the clip");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  auto& adapter = impl_->adapters[impl_->active_adapter];
  if (!adapter.seek) {
    set_diagnostic(diagnostic, "the active RAW adapter does not expose explicit seek");
    return DIGITOR_RESULT_UNSUPPORTED;
  }
  std::string error;
  const auto result = adapter.seek(impl_->session, frame_index, error);
  if (result == DIGITOR_RESULT_OK) impl_->clear_cache();
  else ++impl_->telemetry.adapter_failures;
  set_diagnostic(diagnostic, error);
  return result;
}

void CameraRawEngine::close() noexcept {
  if (!impl_) return;
  std::scoped_lock lock(impl_->mutex);
  if (impl_->session && impl_->active_adapter < impl_->adapters.size()) {
    impl_->adapters[impl_->active_adapter].close(impl_->session);
  }
  impl_->session.reset();
  impl_->active_adapter = static_cast<std::size_t>(-1);
  impl_->source_path.clear();
  impl_->metadata = {};
  impl_->telemetry.active_adapter.clear();
  impl_->clear_cache();
}

void CameraRawEngine::invalidate_cache() noexcept {
  std::scoped_lock lock(impl_->mutex);
  impl_->clear_cache();
}

bool CameraRawEngine::is_open() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return static_cast<bool>(impl_->session);
}

const RawClipMetadata& CameraRawEngine::metadata() const noexcept { return impl_->metadata; }

std::vector<RawAdapterCapabilities> CameraRawEngine::adapters() const {
  std::scoped_lock lock(impl_->mutex);
  std::vector<RawAdapterCapabilities> result;
  result.reserve(impl_->adapters.size());
  for (const auto& adapter : impl_->adapters) result.push_back(adapter.capabilities);
  return result;
}

CameraRawTelemetry CameraRawEngine::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

DigitorResult CameraRawEngine::validate_settings(const RawDecodeSettings& settings,
                                                 std::string* diagnostic) {
  const bool finite_values = std::isfinite(settings.temperature_kelvin) &&
      std::isfinite(settings.tint) && std::isfinite(settings.iso) &&
      std::isfinite(settings.exposure_stops) &&
      std::isfinite(settings.highlight_recovery) &&
      std::isfinite(settings.shadow_recovery) && std::isfinite(settings.black_level) &&
      std::isfinite(settings.saturation);
  if (!finite_values || settings.temperature_kelvin < 1000.0 ||
      settings.temperature_kelvin > 50000.0 || settings.iso <= 0.0 ||
      settings.highlight_recovery < 0.0 || settings.highlight_recovery > 1.0 ||
      settings.shadow_recovery < 0.0 || settings.shadow_recovery > 1.0 ||
      settings.saturation < 0.0) {
    set_diagnostic(diagnostic, "RAW decode settings are outside supported finite ranges");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

const char* CameraRawEngine::codec_name(RawCodec codec) noexcept {
  switch (codec) {
    case RawCodec::cinema_dng: return "CinemaDNG";
    case RawCodec::blackmagic_raw: return "Blackmagic RAW";
    case RawCodec::redcode_raw: return "REDCODE RAW";
    case RawCodec::arri_raw: return "ARRIRAW";
    case RawCodec::canon_raw: return "Canon RAW";
    case RawCodec::sony_raw: return "Sony RAW";
    case RawCodec::prores_raw: return "ProRes RAW";
    case RawCodec::generic_vendor_raw: return "Generic vendor RAW";
    case RawCodec::unknown: break;
  }
  return "Unknown RAW";
}

}  // namespace digitor
