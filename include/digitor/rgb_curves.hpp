#pragma once

#include "digitor/color.hpp"
#include "digitor/render_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace digitor {

inline constexpr std::uint32_t rgb_curves_parameter_version = 1;
inline constexpr std::size_t rgb_curve_max_points = 64;

enum class CurveInterpolation : std::uint32_t { monotone_cubic = 1 };
enum class CurveExtrapolation : std::uint32_t { constant = 1, linear = 2 };

struct RgbCurvePoint { float x{}; float y{}; };

struct RgbCurveDefinition {
    bool enabled{true};
    std::vector<RgbCurvePoint> points{{0.0f, 0.0f}, {1.0f, 1.0f}};
    CurveInterpolation interpolation{CurveInterpolation::monotone_cubic};
    float domain_min{0.0f};
    float domain_max{1.0f};
    CurveExtrapolation extrapolation{CurveExtrapolation::linear};
};

struct RgbCurvesParameters {
    RgbCurveDefinition master, red, green, blue;
    std::uint32_t lut_size{1024};
};

struct CompiledRgbCurve {
    std::vector<float> samples;
    float domain_min{};
    float domain_max{1.0f};
    float slope_before{1.0f};
    float slope_after{1.0f};
    float first_value{};
    float last_value{1.0f};
    CurveExtrapolation extrapolation{CurveExtrapolation::linear};
    bool enabled{true};
    bool identity{true};
};

class CompiledRgbCurves final {
public:
    static std::shared_ptr<const CompiledRgbCurves> compile(const RgbCurvesParameters&);
    [[nodiscard]] Color apply(Color) const noexcept;
    void apply(std::span<const Color> source, std::span<Color> destination) const;
    [[nodiscard]] const std::array<CompiledRgbCurve, 4>& curves() const noexcept { return curves_; }
    [[nodiscard]] std::uint32_t lut_size() const noexcept { return lut_size_; }
    [[nodiscard]] const std::string& identity() const noexcept { return identity_; }
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static std::size_t cache_size();
    static void clear_cache();
private:
    std::array<CompiledRgbCurve, 4> curves_;
    std::uint32_t lut_size_{};
    std::string identity_;
};

// Adds an explicit CPU-reference pass. Native GPU curve attachment remains
// unsupported until a backend supplies the complete binding/dispatch contract.
GraphPass add_rgb_curves_cpu_pass(RenderGraph&, GraphResource source,
                                  GraphResource destination,
                                  std::shared_ptr<const CompiledRgbCurves>,
                                  std::span<const Color> input,
                                  std::span<Color> output);

} // namespace digitor
