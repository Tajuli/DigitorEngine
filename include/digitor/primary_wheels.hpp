#pragma once

#include "digitor/color.hpp"
#include "digitor/render_graph.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace digitor {

inline constexpr std::uint32_t primary_wheels_parameter_version = 1;

struct PrimaryRgb { float r, g, b; };

// Canonical engine values (not UI coordinates).  Additive controls use zero
// as identity; multiplicative controls use one.
struct PrimaryWheelsDescriptor {
  std::uint32_t schema_version{primary_wheels_parameter_version};
  PrimaryRgb lift{0, 0, 0}; float lift_master{}; bool lift_enabled{true};
  PrimaryRgb gamma{1, 1, 1}; float gamma_master{1}; bool gamma_enabled{true};
  PrimaryRgb gain{1, 1, 1}; float gain_master{1}; bool gain_enabled{true};
  PrimaryRgb offset{0, 0, 0}; float offset_master{}; bool offset_enabled{true};
};

// Validated immutable value object. It is safe to share between threads.
class PrimaryWheelsParameters final {
public:
  static std::shared_ptr<const PrimaryWheelsParameters> create(
      const PrimaryWheelsDescriptor& = {});
  [[nodiscard]] const PrimaryWheelsDescriptor& values() const noexcept { return values_; }
  [[nodiscard]] bool is_identity() const noexcept;
  [[nodiscard]] const std::string& identity() const noexcept { return identity_; }
  [[nodiscard]] const std::string& serialize() const noexcept { return serialization_; }
private:
  explicit PrimaryWheelsParameters(PrimaryWheelsDescriptor);
  const PrimaryWheelsDescriptor values_;
  const std::string serialization_;
  const std::string identity_;
};

// Authoritative independent FP32 reference. Alpha is copied bit-for-bit.
[[nodiscard]] Color apply_primary_wheels_reference(
    Color, const PrimaryWheelsParameters&) noexcept;
void apply_primary_wheels_reference(std::span<const Color>, std::span<Color>,
                                    const PrimaryWheelsParameters&);
[[nodiscard]] std::uint64_t primary_wheels_reference_count() noexcept;
void reset_primary_wheels_reference_count() noexcept;

GraphPass add_primary_wheels_cpu_pass(RenderGraph&, GraphResource source,
    GraphResource destination, std::shared_ptr<const PrimaryWheelsParameters>,
    std::span<const Color> input, std::span<Color> output);
GraphPass add_primary_wheels_pass(RenderGraph&, GraphResource source,
    GraphResource destination, std::function<void(CommandEncoder&)> native_execute);

} // namespace digitor
