#pragma once

#include "digitor/digitor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class RawCodec {
  unknown,
  cinema_dng,
  blackmagic_raw,
  redcode_raw,
  arri_raw,
  canon_raw,
  sony_raw,
  prores_raw,
  generic_vendor_raw
};

enum class RawDecodeQuality { draft, half, full };
enum class RawWhiteBalanceMode { camera, auto_neutral, manual };
enum class RawOutputEncoding { camera_native, log, scene_linear };

struct RawClipMetadata {
  RawCodec codec{RawCodec::unknown};
  std::string vendor;
  std::string camera_model;
  std::string camera_serial;
  std::string reel_name;
  std::string timecode;
  std::string sensor_pattern;
  std::string native_gamut;
  std::string native_transfer;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t bit_depth{};
  std::uint64_t frame_count{};
  double frame_rate{};
  double duration_seconds{};
  double camera_iso{};
  double camera_temperature_kelvin{};
  double camera_tint{};
  bool supports_gpu_decode{};
  bool supports_highlight_recovery{};
  bool supports_black_level{};
  bool supports_lens_metadata{};
};

struct RawDecodeSettings {
  RawDecodeQuality quality{RawDecodeQuality::full};
  RawWhiteBalanceMode white_balance{RawWhiteBalanceMode::camera};
  RawOutputEncoding output_encoding{RawOutputEncoding::scene_linear};
  double temperature_kelvin{6500.0};
  double tint{};
  double iso{800.0};
  double exposure_stops{};
  double highlight_recovery{};
  double shadow_recovery{};
  double black_level{};
  double saturation{1.0};
  bool use_camera_metadata{true};
  bool prefer_gpu{true};
  bool require_gpu{};
  bool preserve_negative_values{true};
};

struct RawDecodeRequest {
  std::string source_path;
  std::uint64_t frame_index{};
  double timestamp_seconds{};
  RawDecodeSettings settings;
};

struct RawDecodedFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  DigitorPixelFormat format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  double timestamp_seconds{};
  std::uint64_t frame_index{};
  std::shared_ptr<void> native_surface;
  std::vector<float> rgba32f;
  std::string output_gamut;
  std::string output_transfer;
  bool gpu_resident{};
};

struct RawAdapterCapabilities {
  std::string name;
  std::string version;
  std::vector<RawCodec> codecs;
  bool gpu_decode{};
  bool cpu_decode{};
  bool random_access{};
  bool metadata_only_probe{};
  bool debayer_quality_control{};
  bool white_balance_control{};
  bool iso_control{};
  bool exposure_control{};
  bool highlight_recovery{};
};

struct RawAdapterCallbacks {
  RawAdapterCapabilities capabilities;
  std::function<DigitorResult(const std::string&, RawClipMetadata&, std::string&)> probe;
  std::function<DigitorResult(const std::string&, std::shared_ptr<void>&, std::string&)> open;
  std::function<DigitorResult(const std::shared_ptr<void>&, const RawDecodeRequest&,
                              RawDecodedFrame&, std::string&)> decode;
  std::function<DigitorResult(const std::shared_ptr<void>&, std::uint64_t,
                              std::string&)> seek;
  std::function<void(std::shared_ptr<void>&)> close;
};

struct CameraRawTelemetry {
  std::uint64_t registered_adapters{};
  std::uint64_t probes{};
  std::uint64_t opens{};
  std::uint64_t decoded_frames{};
  std::uint64_t gpu_frames{};
  std::uint64_t cpu_frames{};
  std::uint64_t cache_hits{};
  std::uint64_t rejected_requests{};
  std::uint64_t adapter_failures{};
  std::string active_adapter;
  std::string last_error;
};

class CameraRawEngine {
 public:
  explicit CameraRawEngine(std::size_t frame_cache_capacity = 8);
  ~CameraRawEngine();

  CameraRawEngine(CameraRawEngine&&) noexcept;
  CameraRawEngine& operator=(CameraRawEngine&&) noexcept;
  CameraRawEngine(const CameraRawEngine&) = delete;
  CameraRawEngine& operator=(const CameraRawEngine&) = delete;

  DigitorResult register_adapter(RawAdapterCallbacks adapter,
                                 std::string* diagnostic = nullptr);
  DigitorResult probe(const std::string& source_path, RawClipMetadata& metadata,
                      std::string* diagnostic = nullptr);
  DigitorResult open(const std::string& source_path,
                     std::string* diagnostic = nullptr);
  DigitorResult decode(const RawDecodeRequest& request, RawDecodedFrame& frame,
                       std::string* diagnostic = nullptr);
  DigitorResult seek(std::uint64_t frame_index,
                     std::string* diagnostic = nullptr);
  void close() noexcept;
  void invalidate_cache() noexcept;

  bool is_open() const noexcept;
  const RawClipMetadata& metadata() const noexcept;
  std::vector<RawAdapterCapabilities> adapters() const;
  CameraRawTelemetry telemetry() const;

  static DigitorResult validate_settings(const RawDecodeSettings& settings,
                                         std::string* diagnostic = nullptr);
  static const char* codec_name(RawCodec codec) noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace digitor
