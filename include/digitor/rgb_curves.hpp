#pragma once

#include "digitor/color.hpp"
#include "digitor/render_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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

enum class RgbCurvesBackend : std::uint32_t {
    cpu = 0, vulkan = 1, d3d12 = 2, metal = 3, gles = 4
};

enum class RgbCurvesPrecision : std::uint32_t { fp32 = 1 };

// Immutable native-resource key. Device identity is deliberately part of the
// key: native objects are never shared between physical/logical devices.
struct NativeRgbCurvesKey {
    std::string compiled_identity;
    std::string device_identity;
    RgbCurvesBackend backend{RgbCurvesBackend::cpu};
    RgbCurvesPrecision precision{RgbCurvesPrecision::fp32};
    std::uint32_t lut_size{};
    std::uint32_t interpolation_version{1};
    std::uint32_t shader_abi_version{1};
    bool operator==(const NativeRgbCurvesKey&) const noexcept = default;
    [[nodiscard]] std::string serialize() const;
};

// Adds the explicit CPU-reference pass; GPU backends never call this executor.
GraphPass add_rgb_curves_cpu_pass(RenderGraph&, GraphResource source,
                                  GraphResource destination,
                                  std::shared_ptr<const CompiledRgbCurves>,
                                  std::span<const Color> input,
                                  std::span<Color> output);

// Backend-neutral graph pass. GPU work is submitted only by the selected
// backend; an error is returned by that backend rather than falling back.
GraphPass add_rgb_curves_pass(RenderGraph&, GraphResource source,
                              GraphResource destination,
                              std::function<void(CommandEncoder&)> native_execute);

} // namespace digitor
