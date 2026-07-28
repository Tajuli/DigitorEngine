#pragma once

#include "digitor/color.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace digitor::color {

enum class PixelFormat { unknown, rgba8, rgba16_float, rgba32_float, nv12, yuv420p };
enum class Primaries { unspecified, bt601_525, bt601_625, bt709, bt2020, display_p3 };
enum class Transfer { unspecified, linear, srgb, bt709, bt1886, gamma22, gamma24, pq, hlg };
enum class MatrixCoefficients { unspecified, identity, bt601, bt709, bt2020_ncl };
enum class Range { unspecified, full, limited };
enum class ChromaLocation { unspecified, top_left, left, center };
enum class Alpha { unspecified, opaque, straight, premultiplied };
enum class Space { unknown, bt601, bt709, bt2020, srgb, display_p3,
                   linear_srgb, linear_bt709, linear_bt2020 };

struct MasteringMetadata {
  bool present{};
  std::array<double, 8> primaries_and_white_xy{};
  double minimum_luminance_cd_m2{};
  double maximum_luminance_cd_m2{};
};
struct ContentLightMetadata { bool present{}; std::uint16_t max_cll{}, max_fall{}; };
struct Metadata {
  PixelFormat pixel_format{PixelFormat::unknown};
  std::uint8_t bit_depth{};
  Primaries primaries{Primaries::unspecified};
  Transfer transfer{Transfer::unspecified};
  MatrixCoefficients matrix{MatrixCoefficients::unspecified};
  Range range{Range::unspecified};
  ChromaLocation chroma_location{ChromaLocation::unspecified};
  Alpha alpha{Alpha::unspecified};
  MasteringMetadata mastering{};
  ContentLightMetadata content_light{};
  Space source{Space::unknown}, working{Space::unknown}, output{Space::unknown};
};
struct MetadataResolution { Metadata metadata; bool used_fallback{}; std::string decision; };
MetadataResolution resolve_metadata(const Metadata&, const Metadata& explicit_fallback);

// Scalar FP32 reference. Non-finite inputs and negative PQ/HLG inputs are rejected.
float decode(Transfer, float encoded);
float encode(Transfer, float linear);

struct Chromaticity { double x{}, y{}; };
struct Chromaticities { Chromaticity red, green, blue, white; };
struct Vec3 { double x{}, y{}, z{}; };
struct Mat3 { std::array<double, 9> v{}; };
Chromaticities chromaticities(Primaries);
Mat3 rgb_to_xyz(const Chromaticities&);
Mat3 inverse(const Mat3&);
Mat3 multiply(const Mat3&, const Mat3&);
Vec3 multiply(const Mat3&, Vec3);
Mat3 rgb_to_rgb(const Chromaticities& source, const Chromaticities& destination);
enum class Adaptation { bradford };
Mat3 adaptation_matrix(Adaptation, Chromaticity source_white, Chromaticity destination_white);

struct YuvCode { std::uint16_t y{}, u{}, v{}; };
Vec3 yuv_to_rgb(YuvCode, MatrixCoefficients, Range, unsigned bit_depth);

enum class ToneMap { passthrough, diagnostic_hard_clip, reinhard };
Vec3 tone_map(Vec3, ToneMap);

enum class StageKind { yuv_to_rgb, decode_transfer, chromatic_adaptation,
                       primaries_conversion, working_conversion, color_operation,
                       output_primaries_conversion, encode_transfer, tone_map };
struct Stage { StageKind kind{}; std::array<double, 9> parameters{}; };
class TransformGraph {
public:
  static TransformGraph compile(std::span<const Stage>);
  std::span<const Stage> stages() const noexcept { return stages_; }
  std::uint64_t identity() const noexcept { return identity_; }
  Color execute(Color) const;
private:
  std::vector<Stage> stages_;
  std::uint64_t identity_{};
};

enum class FutureTool { rgb_curves, primary_wheels, log_wheels, hsl_qualifier, lut };
struct FutureToolContract {
  Space input{Space::linear_bt709}, output{Space::linear_bt709};
  StageKind placement{StageKind::color_operation};
  bool preserves_alpha{true}, deterministic_serialization{true}, cpu_gpu_parity_required{true};
  const char* parameter_range{};
};
FutureToolContract future_tool_contract(FutureTool);

} // namespace digitor::color
