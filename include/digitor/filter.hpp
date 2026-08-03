#pragma once

#include "digitor/color.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class FilterCategory {
    basic,
    cinematic,
    film,
    portrait,
    food,
    landscape,
    night,
    seasonal,
    monochrome,
    creative
};

struct FilterPreset {
    std::string id;
    std::string name;
    FilterCategory category{FilterCategory::basic};
    ColorGrade grade{};
    std::array<float, 9> matrix{1,0,0, 0,1,0, 0,0,1};
    Color offset{0,0,0,0};
    float fade{};
    bool hdr_safe{true};
};

struct FilterInstance {
    std::string preset_id;
    float intensity{1.0f};
    bool enabled{true};
};

class FilterRegistry {
public:
    FilterRegistry();
    explicit FilterRegistry(std::vector<FilterPreset> presets);

    const std::vector<FilterPreset>& presets() const noexcept;
    const FilterPreset* find(std::string_view id) const noexcept;
    std::vector<const FilterPreset*> category(FilterCategory value) const;
    bool add(FilterPreset preset);

private:
    std::vector<FilterPreset> presets_;
};

class FilterStack {
public:
    bool add(FilterInstance instance);
    bool remove(std::size_t index) noexcept;
    void clear() noexcept;
    const std::vector<FilterInstance>& entries() const noexcept;
    std::string serialize() const;
    static std::optional<FilterStack> deserialize(std::string_view text);

private:
    std::vector<FilterInstance> entries_;
};

Color apply_filter(Color input, const FilterPreset& preset, float intensity = 1.0f) noexcept;
void apply_filter_cpu(const Color* input, Color* output, std::size_t count,
                      const FilterPreset& preset, float intensity = 1.0f);
void apply_filter_gpu(CommandEncoder& encoder, const Color* input, Color* output,
                      std::size_t count, const FilterPreset& preset,
                      float intensity = 1.0f);
void apply_filter_stack_cpu(const Color* input, Color* output, std::size_t count,
                            const FilterRegistry& registry, const FilterStack& stack);
void apply_filter_stack_gpu(CommandEncoder& encoder, const Color* input, Color* output,
                            std::size_t count, const FilterRegistry& registry,
                            const FilterStack& stack);

std::vector<FilterPreset> built_in_filters();
const char* filter_category_name(FilterCategory category) noexcept;

} // namespace digitor
