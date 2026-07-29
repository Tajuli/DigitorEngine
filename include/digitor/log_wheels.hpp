#pragma once

#include "digitor/color.hpp"
#include "digitor/primary_wheels.hpp"
#include "digitor/render_graph.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace digitor {

inline constexpr std::uint32_t log_wheels_parameter_version = 1;

struct LogWheelControl {
  PrimaryRgb rgb{0, 0, 0};
  float master{}; // exposure in stops for this tonal band
  bool enabled{true};
};

struct LogWheelsDescriptor {
  std::uint32_t schema_version{log_wheels_parameter_version};
  LogWheelControl shadows{};
  LogWheelControl midtones{};
  LogWheelControl highlights{};
  LogWheelControl global{};
  float shadow_pivot{0.25f};
  float highlight_pivot{0.75f};
  float transition_width{0.20f};
};

class LogWheelsParameters final {
public:
  static std::shared_ptr<const LogWheelsParameters> create(
      const LogWheelsDescriptor& = {});
  [[nodiscard]] const LogWheelsDescriptor& values() const noexcept { return values_; }
  [[nodiscard]] bool is_identity() const noexcept;
  [[nodiscard]] const std::string& identity() const noexcept { return identity_; }
  [[nodiscard]] const std::string& serialize() const noexcept { return serialization_; }
private:
  explicit LogWheelsParameters(LogWheelsDescriptor);
  const LogWheelsDescriptor values_;
  const std::string serialization_;
  const std::string identity_;
};

[[nodiscard]] Color apply_log_wheels_reference(
    Color, const LogWheelsParameters&) noexcept;
void apply_log_wheels_reference(std::span<const Color>, std::span<Color>,
                                const LogWheelsParameters&);
[[nodiscard]] std::uint64_t log_wheels_reference_count() noexcept;
void reset_log_wheels_reference_count() noexcept;

GraphPass add_log_wheels_cpu_pass(RenderGraph&, GraphResource source,
    GraphResource destination, std::shared_ptr<const LogWheelsParameters>,
    std::span<const Color> input, std::span<Color> output);
GraphPass add_log_wheels_pass(RenderGraph&, GraphResource source,
    GraphResource destination, std::function<void(CommandEncoder&)> native_execute);

} // namespace digitor
