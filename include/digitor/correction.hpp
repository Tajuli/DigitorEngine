#pragma once

#include "digitor/color.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace digitor {

inline constexpr std::uint32_t correction_parameter_version = 1;

struct CorrectionSettings {
  std::uint32_t schema_version{correction_parameter_version};
  float exposure{};
  float contrast{};
  float saturation{};
  float temperature{};
  float tint{};
  float highlights{};
  float shadows{};
  float hue{};
  float color_boost{};
};

class CorrectionParameters final {
public:
  static std::shared_ptr<const CorrectionParameters> create(
      const CorrectionSettings& settings = {});

  [[nodiscard]] const CorrectionSettings& values() const noexcept { return values_; }
  [[nodiscard]] std::string_view serialize() const noexcept { return serialization_; }
  [[nodiscard]] std::string_view identity() const noexcept { return identity_; }
  [[nodiscard]] bool is_identity() const noexcept;

private:
  explicit CorrectionParameters(CorrectionSettings settings);
  CorrectionSettings values_{};
  std::string serialization_;
  std::string identity_;
};

[[nodiscard]] Color apply_correction_reference(
    Color color, const CorrectionParameters& parameters) noexcept;
void apply_correction_reference(std::span<const Color> input,
                                std::span<Color> output,
                                const CorrectionParameters& parameters);

} // namespace digitor
