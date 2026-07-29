#pragma once

#include "digitor/color.hpp"
#include "digitor/digitor.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace digitor {

inline constexpr std::uint32_t hsl_qualifier_parameter_version = 1;

struct QualifierRange {
  float low{};
  float high{1.0f};
  float softness{};
};

struct QualifierSettings {
  std::uint32_t schema_version{hsl_qualifier_parameter_version};
  QualifierRange hue{};
  QualifierRange saturation{};
  QualifierRange luminance{};
  float blur{};
  float denoise{};
  float clean_black{};
  float clean_white{};
  bool invert{};
  bool matte_output{};
};

class HslQualifierParameters final {
public:
  static std::shared_ptr<const HslQualifierParameters> create(
      const QualifierSettings& = {});

  [[nodiscard]] const QualifierSettings& values() const noexcept {
    return values_;
  }
  [[nodiscard]] bool is_identity() const noexcept;
  [[nodiscard]] const std::string& identity() const noexcept {
    return identity_;
  }
  [[nodiscard]] const std::string& serialize() const noexcept {
    return serialization_;
  }

private:
  explicit HslQualifierParameters(QualifierSettings);

  const QualifierSettings values_;
  const std::string serialization_;
  const std::string identity_;
};

// Opaque GPU-resident single-channel matte. Native backend objects are owned by
// the shared native_owner token; destruction therefore occurs on the backend's
// chosen lifetime path without exposing API-specific handles publicly.
class GpuMatteResource final {
public:
  using NativeOwner = std::shared_ptr<void>;

  GpuMatteResource(DigitorRendererBackend backend, std::uint32_t width,
                   std::uint32_t height, std::uint64_t identity,
                   NativeOwner native_owner) noexcept
      : backend_(backend), width_(width), height_(height), identity_(identity),
        native_owner_(std::move(native_owner)) {}

  [[nodiscard]] DigitorRendererBackend backend() const noexcept {
    return backend_;
  }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint64_t identity() const noexcept { return identity_; }
  [[nodiscard]] bool ready() const noexcept {
    return backend_ != DIGITOR_RENDERER_CPU && width_ != 0 && height_ != 0 &&
           identity_ != 0 && static_cast<bool>(native_owner_);
  }

private:
  friend class IRenderBackend;
  DigitorRendererBackend backend_{DIGITOR_RENDERER_CPU};
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::uint64_t identity_{};
  NativeOwner native_owner_{};
};

using GpuMatteResourcePtr = std::shared_ptr<GpuMatteResource>;

class HslQualifier {
public:
  void sample(Color);
  void sample(std::span<const Color>);

  [[nodiscard]] const QualifierSettings& settings() const noexcept {
    return settings_;
  }
  void set_settings(QualifierSettings settings);

  [[nodiscard]] std::vector<float> matte_cpu(
      std::span<const Color>, std::uint32_t width,
      std::uint32_t height) const;

  // Legacy command-encoder entry point retained for source compatibility.
  // It must never execute the CPU reference while presenting itself as GPU
  // work. Production GPU execution is provided by native backend methods.
  void matte_gpu(CommandEncoder&, std::span<const Color>, std::span<float>,
                 std::uint32_t width, std::uint32_t height) const;

private:
  QualifierSettings settings_{};
};

[[nodiscard]] float apply_hsl_qualifier_reference(
    Color, const HslQualifierParameters&) noexcept;
void apply_hsl_qualifier_reference(std::span<const Color>, std::span<float>,
                                   const HslQualifierParameters&);
[[nodiscard]] std::uint64_t hsl_qualifier_reference_count() noexcept;
void reset_hsl_qualifier_reference_count() noexcept;

} // namespace digitor
