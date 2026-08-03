#include "digitor/filter.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace digitor {
namespace {

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

Color mix_color(Color a, Color b, float t) noexcept {
    t = clamp01(t);
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a
    };
}

Color apply_matrix(Color value, const std::array<float, 9>& matrix, Color offset) noexcept {
    return {
        value.r * matrix[0] + value.g * matrix[1] + value.b * matrix[2] + offset.r,
        value.r * matrix[3] + value.g * matrix[4] + value.b * matrix[5] + offset.g,
        value.r * matrix[6] + value.g * matrix[7] + value.b * matrix[8] + offset.b,
        value.a
    };
}

FilterPreset make_preset(std::string id, std::string name, FilterCategory category,
                         ColorGrade grade, std::array<float, 9> matrix,
                         Color offset = {}, float fade = 0.0f, bool hdr_safe = true) {
    FilterPreset preset;
    preset.id = std::move(id);
    preset.name = std::move(name);
    preset.category = category;
    preset.grade = grade;
    preset.matrix = matrix;
    preset.offset = offset;
    preset.fade = fade;
    preset.hdr_safe = hdr_safe;
    return preset;
}

ColorGrade grade(float exposure, float contrast, float saturation,
                 float temperature = 0.0f, float tint = 0.0f,
                 float vibrance = 0.0f, float gamma = 1.0f) {
    ColorGrade result;
    result.exposure = exposure;
    result.contrast = contrast;
    result.saturation = saturation;
    result.temperature = temperature;
    result.tint = tint;
    result.vibrance = vibrance;
    result.gamma = gamma;
    return result;
}

constexpr std::array<float, 9> identity_matrix{1,0,0, 0,1,0, 0,0,1};
constexpr std::array<float, 9> warm_matrix{1.05f,0.01f,-0.02f, 0.01f,1.00f,-0.01f, -0.03f,0.01f,0.96f};
constexpr std::array<float, 9> cool_matrix{0.96f,0.00f,0.03f, 0.00f,1.00f,0.01f, 0.01f,0.01f,1.05f};
constexpr std::array<float, 9> teal_amber_matrix{1.05f,0.02f,-0.05f, -0.02f,1.02f,0.01f, -0.06f,0.04f,1.08f};
constexpr std::array<float, 9> mono_matrix{0.299f,0.587f,0.114f, 0.299f,0.587f,0.114f, 0.299f,0.587f,0.114f};
constexpr std::array<float, 9> green_print_matrix{0.95f,0.03f,0.00f, 0.02f,1.04f,0.00f, 0.00f,0.04f,0.92f};
constexpr std::array<float, 9> magenta_matrix{1.04f,-0.01f,0.02f, 0.00f,0.97f,0.01f, 0.03f,-0.01f,1.04f};

} // namespace

std::vector<FilterPreset> built_in_filters() {
    return {
        make_preset("clean", "Clean", FilterCategory::basic,
                    grade(0.0f, 1.0f, 1.0f), identity_matrix),
        make_preset("vivid_pop", "Vivid Pop", FilterCategory::basic,
                    grade(0.05f, 1.10f, 1.18f, 0, 0, 0.15f), identity_matrix),
        make_preset("soft_fade", "Soft Fade", FilterCategory::basic,
                    grade(0.02f, 0.90f, 0.92f, 0, 0, 0.05f), identity_matrix, {0.015f,0.015f,0.015f,0}, 0.12f),
        make_preset("crisp_day", "Crisp Day", FilterCategory::basic,
                    grade(0.03f, 1.12f, 1.08f, -0.02f, 0, 0.08f), identity_matrix),

        make_preset("cinema_warm", "Cinema Warm", FilterCategory::cinematic,
                    grade(-0.02f, 1.13f, 0.94f, 0.08f, 0.01f, 0.04f), warm_matrix),
        make_preset("cinema_cool", "Cinema Cool", FilterCategory::cinematic,
                    grade(-0.04f, 1.14f, 0.92f, -0.08f, 0, 0.03f), cool_matrix),
        make_preset("teal_amber", "Teal Amber", FilterCategory::cinematic,
                    grade(-0.03f, 1.16f, 1.02f, 0.02f, 0, 0.08f), teal_amber_matrix),
        make_preset("moody_frame", "Moody Frame", FilterCategory::cinematic,
                    grade(-0.10f, 1.18f, 0.82f, -0.03f, 0.02f, -0.02f), cool_matrix),

        make_preset("film_print", "Film Print", FilterCategory::film,
                    grade(-0.03f, 1.05f, 0.90f, 0.04f, 0.02f, 0.02f, 0.96f), warm_matrix, {0.012f,0.008f,0.004f,0}, 0.08f),
        make_preset("vintage_green", "Vintage Green", FilterCategory::film,
                    grade(-0.02f, 0.98f, 0.82f, 0.02f, -0.02f, -0.03f), green_print_matrix, {0.018f,0.022f,0.012f,0}, 0.15f),
        make_preset("retro_magenta", "Retro Magenta", FilterCategory::film,
                    grade(0.00f, 1.02f, 0.88f, 0.01f, 0.06f, 0.02f), magenta_matrix, {0.012f,0.005f,0.014f,0}, 0.10f),
        make_preset("washed_negative", "Washed Negative", FilterCategory::film,
                    grade(0.04f, 0.86f, 0.76f, 0.03f, 0, -0.04f), identity_matrix, {0.025f,0.020f,0.018f,0}, 0.22f),

        make_preset("portrait_warm", "Portrait Warm", FilterCategory::portrait,
                    grade(0.05f, 0.98f, 0.94f, 0.07f, 0.02f, 0.08f), warm_matrix),
        make_preset("skin_glow", "Skin Glow", FilterCategory::portrait,
                    grade(0.08f, 0.94f, 0.90f, 0.05f, 0.03f, 0.10f), identity_matrix, {0.01f,0.006f,0.004f,0}, 0.05f),
        make_preset("portrait_clean", "Portrait Clean", FilterCategory::portrait,
                    grade(0.03f, 1.00f, 0.96f, 0.02f, 0.01f, 0.05f), identity_matrix),

        make_preset("food_pop", "Food Pop", FilterCategory::food,
                    grade(0.06f, 1.08f, 1.20f, 0.06f, 0.01f, 0.14f), warm_matrix),
        make_preset("cafe_warm", "Cafe Warm", FilterCategory::food,
                    grade(-0.01f, 1.02f, 1.04f, 0.10f, 0.02f, 0.08f), warm_matrix, {0.006f,0.003f,0,0}, 0.04f),

        make_preset("landscape_crisp", "Landscape Crisp", FilterCategory::landscape,
                    grade(0.02f, 1.15f, 1.12f, -0.02f, 0, 0.16f), identity_matrix),
        make_preset("forest_depth", "Forest Depth", FilterCategory::landscape,
                    grade(-0.04f, 1.12f, 1.03f, -0.01f, -0.02f, 0.10f), green_print_matrix),
        make_preset("ocean_clear", "Ocean Clear", FilterCategory::landscape,
                    grade(0.02f, 1.09f, 1.10f, -0.07f, -0.01f, 0.12f), cool_matrix),

        make_preset("night_blue", "Night Blue", FilterCategory::night,
                    grade(-0.10f, 1.13f, 0.88f, -0.12f, 0.01f, 0.04f), cool_matrix),
        make_preset("neon_city", "Neon City", FilterCategory::night,
                    grade(-0.05f, 1.20f, 1.22f, -0.04f, 0.06f, 0.18f), magenta_matrix),

        make_preset("sunset_gold", "Sunset Gold", FilterCategory::seasonal,
                    grade(0.03f, 1.08f, 1.10f, 0.13f, 0.02f, 0.10f), warm_matrix),
        make_preset("autumn_leaf", "Autumn Leaf", FilterCategory::seasonal,
                    grade(-0.01f, 1.10f, 1.04f, 0.11f, 0.03f, 0.08f), warm_matrix),
        make_preset("winter_air", "Winter Air", FilterCategory::seasonal,
                    grade(0.03f, 1.02f, 0.88f, -0.13f, -0.01f, 0.04f), cool_matrix),

        make_preset("mono_soft", "Mono Soft", FilterCategory::monochrome,
                    grade(0.02f, 0.94f, 1.0f), mono_matrix, {0.01f,0.01f,0.01f,0}, 0.08f),
        make_preset("mono_contrast", "Mono Contrast", FilterCategory::monochrome,
                    grade(-0.02f, 1.22f, 1.0f), mono_matrix),
        make_preset("documentary_bw", "Documentary B&W", FilterCategory::monochrome,
                    grade(-0.04f, 1.12f, 1.0f), mono_matrix, {0.005f,0.005f,0.005f,0}, 0.03f),

        make_preset("dream_haze", "Dream Haze", FilterCategory::creative,
                    grade(0.08f, 0.86f, 0.88f, 0.02f, 0.04f, 0.04f), magenta_matrix, {0.025f,0.02f,0.03f,0}, 0.18f),
        make_preset("urban_matte", "Urban Matte", FilterCategory::creative,
                    grade(-0.05f, 1.02f, 0.78f, -0.03f, 0, -0.02f), identity_matrix, {0.018f,0.018f,0.018f,0}, 0.16f)
    };
}

FilterRegistry::FilterRegistry() : presets_(built_in_filters()) {}

FilterRegistry::FilterRegistry(std::vector<FilterPreset> presets) {
    for (auto& preset : presets) add(std::move(preset));
}

const std::vector<FilterPreset>& FilterRegistry::presets() const noexcept {
    return presets_;
}

const FilterPreset* FilterRegistry::find(std::string_view id) const noexcept {
    const auto it = std::find_if(presets_.begin(), presets_.end(),
        [&](const FilterPreset& preset) { return preset.id == id; });
    return it == presets_.end() ? nullptr : &*it;
}

std::vector<const FilterPreset*> FilterRegistry::category(FilterCategory value) const {
    std::vector<const FilterPreset*> result;
    for (const auto& preset : presets_) {
        if (preset.category == value) result.push_back(&preset);
    }
    return result;
}

bool FilterRegistry::add(FilterPreset preset) {
    if (preset.id.empty() || preset.name.empty() || find(preset.id)) return false;
    presets_.push_back(std::move(preset));
    return true;
}

bool FilterStack::add(FilterInstance instance) {
    if (instance.preset_id.empty() || !std::isfinite(instance.intensity)) return false;
    instance.intensity = clamp01(instance.intensity);
    entries_.push_back(std::move(instance));
    return true;
}

bool FilterStack::remove(std::size_t index) noexcept {
    if (index >= entries_.size()) return false;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void FilterStack::clear() noexcept { entries_.clear(); }

const std::vector<FilterInstance>& FilterStack::entries() const noexcept { return entries_; }

std::string FilterStack::serialize() const {
    std::ostringstream out;
    out << "DIGITOR_FILTER_STACK_V1\n";
    for (const auto& entry : entries_) {
        out << entry.preset_id << '\t' << entry.intensity << '\t' << (entry.enabled ? 1 : 0) << '\n';
    }
    return out.str();
}

std::optional<FilterStack> FilterStack::deserialize(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line) || line != "DIGITOR_FILTER_STACK_V1") return std::nullopt;
    FilterStack stack;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) return std::nullopt;
        FilterInstance entry;
        entry.preset_id = line.substr(0, first);
        try {
            entry.intensity = std::stof(line.substr(first + 1, second - first - 1));
            const auto enabled = std::stoi(line.substr(second + 1));
            if ((enabled != 0 && enabled != 1) || !std::isfinite(entry.intensity)) return std::nullopt;
            entry.enabled = enabled == 1;
        } catch (...) {
            return std::nullopt;
        }
        if (!stack.add(std::move(entry))) return std::nullopt;
    }
    return stack;
}

Color apply_filter(Color input, const FilterPreset& preset, float intensity) noexcept {
    intensity = clamp01(std::isfinite(intensity) ? intensity : 0.0f);
    Color transformed = grade_color(input, preset.grade);
    transformed = apply_matrix(transformed, preset.matrix, preset.offset);
    if (preset.fade > 0.0f) {
        const float luma = transformed.r * 0.2126f + transformed.g * 0.7152f + transformed.b * 0.0722f;
        const Color faded{
            transformed.r * (1.0f - preset.fade) + luma * preset.fade,
            transformed.g * (1.0f - preset.fade) + luma * preset.fade,
            transformed.b * (1.0f - preset.fade) + luma * preset.fade,
            input.a
        };
        transformed = faded;
    }
    transformed.a = input.a;
    return mix_color(input, transformed, intensity);
}

void apply_filter_cpu(const Color* input, Color* output, std::size_t count,
                      const FilterPreset& preset, float intensity) {
    if ((!input || !output) && count != 0) throw std::invalid_argument("invalid filter image");
    for (std::size_t i = 0; i < count; ++i) output[i] = apply_filter(input[i], preset, intensity);
}

void apply_filter_gpu(CommandEncoder& encoder, const Color* input, Color* output,
                      std::size_t count, const FilterPreset& preset, float intensity) {
    if ((!input || !output) && count != 0) throw std::invalid_argument("invalid filter image");
    encoder.dispatch([=] { apply_filter_cpu(input, output, count, preset, intensity); });
}

void apply_filter_stack_cpu(const Color* input, Color* output, std::size_t count,
                            const FilterRegistry& registry, const FilterStack& stack) {
    if ((!input || !output) && count != 0) throw std::invalid_argument("invalid filter stack image");
    if (count == 0) return;
    std::vector<Color> current(input, input + count);
    std::vector<Color> next(count);
    for (const auto& entry : stack.entries()) {
        if (!entry.enabled || entry.intensity <= 0.0f) continue;
        const auto* preset = registry.find(entry.preset_id);
        if (!preset) throw std::invalid_argument("unknown filter preset");
        apply_filter_cpu(current.data(), next.data(), count, *preset, entry.intensity);
        current.swap(next);
    }
    std::copy(current.begin(), current.end(), output);
}

void apply_filter_stack_gpu(CommandEncoder& encoder, const Color* input, Color* output,
                            std::size_t count, const FilterRegistry& registry,
                            const FilterStack& stack) {
    if ((!input || !output) && count != 0) throw std::invalid_argument("invalid filter stack image");
    const auto presets = registry.presets();
    const auto serialized = stack.serialize();
    encoder.dispatch([=] {
        const FilterRegistry captured_registry(presets);
        const auto captured_stack = FilterStack::deserialize(serialized);
        if (!captured_stack) throw std::runtime_error("invalid captured filter stack");
        apply_filter_stack_cpu(input, output, count, captured_registry, *captured_stack);
    });
}

const char* filter_category_name(FilterCategory category) noexcept {
    switch (category) {
        case FilterCategory::basic: return "Basic";
        case FilterCategory::cinematic: return "Cinematic";
        case FilterCategory::film: return "Film";
        case FilterCategory::portrait: return "Portrait";
        case FilterCategory::food: return "Food";
        case FilterCategory::landscape: return "Landscape";
        case FilterCategory::night: return "Night";
        case FilterCategory::seasonal: return "Seasonal";
        case FilterCategory::monochrome: return "Monochrome";
        case FilterCategory::creative: return "Creative";
    }
    return "Unknown";
}

} // namespace digitor
