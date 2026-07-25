#pragma once

#include "digitor/color.hpp"
#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace digitor {
enum class LutInterpolation { nearest, linear, tetrahedral };

class Lut1D {
public:
    explicit Lut1D(std::vector<Color> values = {});
    Color sample(Color color, LutInterpolation interpolation = LutInterpolation::linear) const;
    const std::vector<Color>& values() const noexcept { return values_; }
private:
    std::vector<Color> values_;
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
    std::size_t size_{};
    std::vector<Color> values_;
    Color domain_min_{0,0,0,1}, domain_max_{1,1,1,1};
};

void apply_lut_cpu(const Color*, Color*, std::size_t, const Lut1D&, LutInterpolation = LutInterpolation::linear);
void apply_lut_cpu(const Color*, Color*, std::size_t, const Lut3D&, LutInterpolation = LutInterpolation::tetrahedral);
void apply_lut_gpu(CommandEncoder&, const Color*, Color*, std::size_t, const Lut1D&, LutInterpolation = LutInterpolation::linear);
void apply_lut_gpu(CommandEncoder&, const Color*, Color*, std::size_t, const Lut3D&, LutInterpolation = LutInterpolation::tetrahedral);
}
