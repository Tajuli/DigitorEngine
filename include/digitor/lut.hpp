#pragma once

#include "digitor/color.hpp"
#include <cstddef>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace digitor {
enum class LutInterpolation { nearest, linear, tetrahedral };
struct ParsedCube;

class Lut1D {
public:
    explicit Lut1D(std::vector<Color> values = {});
    Color sample(Color color, LutInterpolation interpolation = LutInterpolation::linear) const;
    const std::vector<Color>& values() const noexcept { return values_; }
    static Lut1D load_cube(std::istream& input);
    static Lut1D load_cube_file(const std::string& path);
private:
    friend ParsedCube parse_cube(std::istream& input);
    std::vector<Color> values_;
    Color domain_min_{0,0,0,1}, domain_max_{1,1,1,1};
};

class Lut3D {
public:
    Lut3D(std::size_t size, std::vector<Color> values);
    static Lut3D load_cube(std::istream& input);
    static Lut3D load_cube_file(const std::string& path);
    Color sample(Color color, LutInterpolation interpolation = LutInterpolation::tetrahedral) const;
    std::size_t size() const noexcept { return size_; }
    const std::vector<Color>& values() const noexcept { return values_; }
private:
    friend ParsedCube parse_cube(std::istream& input);
    std::size_t size_{};
    std::vector<Color> values_;
    Color domain_min_{0,0,0,1}, domain_max_{1,1,1,1};
};

// Parses either LUT_1D_SIZE or LUT_3D_SIZE Cube data. Exactly one pointer is
// populated; this is useful to applications which accept arbitrary .cube files.
struct ParsedCube {
    std::optional<Lut1D> one_dimensional;
    std::optional<Lut3D> three_dimensional;
};
ParsedCube parse_cube(std::istream& input);

void apply_lut_cpu(const Color*, Color*, std::size_t, const Lut1D&, LutInterpolation = LutInterpolation::linear);
void apply_lut_cpu(const Color*, Color*, std::size_t, const Lut3D&, LutInterpolation = LutInterpolation::tetrahedral);
void apply_lut_gpu(CommandEncoder&, const Color*, Color*, std::size_t, const Lut1D&, LutInterpolation = LutInterpolation::linear);
void apply_lut_gpu(CommandEncoder&, const Color*, Color*, std::size_t, const Lut3D&, LutInterpolation = LutInterpolation::tetrahedral);
}
