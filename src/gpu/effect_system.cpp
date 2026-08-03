#include "digitor/effect_system.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace digitor {
namespace {

bool effect_valid_id(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    for (const char c : id) {
        const bool valid = (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') ||
                           c == '.' || c == '_' || c == '-';
        if (!valid) return false;
    }
    return true;
}

bool effect_parse_float(std::string_view text, float& value) {
    if (text.empty()) return false;
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    stream >> std::noskipws >> value;
    return stream && stream.eof() && std::isfinite(value);
}

bool effect_parse_u64(std::string_view text, std::uint64_t& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

float effect_quality_radius(float radius, EffectQuality quality) {
    switch (quality) {
        case EffectQuality::preview: return std::min(radius, 12.0f);
        case EffectQuality::balanced: return std::min(radius, 32.0f);
        case EffectQuality::export_quality: return radius;
    }
    return radius;
}

EffectSettings effect_settings(const EffectDescriptor& descriptor,
                               const EffectInstance& instance,
                               EffectQuality quality) {
    EffectSettings result;
    result.type = descriptor.type;
    result.amount = instance.amount;
    result.radius = effect_quality_radius(instance.radius, quality);
    result.angle = instance.angle;
    result.seed = instance.seed;
    return result;
}

bool effect_validate_frame(const Color* input, Color* output,
                           std::uint32_t width, std::uint32_t height,
                           const float* matte, std::size_t matte_count,
                           std::string& diagnostic) {
    if (!input || !output || !width || !height) {
        diagnostic = "invalid effect image";
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if ((matte && matte_count != count) || (!matte && matte_count != 0)) {
        diagnostic = "effect matte size mismatch";
        return false;
    }
    if (matte) {
        for (std::size_t i = 0; i < matte_count; ++i) {
            if (!std::isfinite(matte[i]) || matte[i] < 0.0f || matte[i] > 1.0f) {
                diagnostic = "effect matte contains invalid value";
                return false;
            }
        }
    }
    return true;
}

void effect_composite_matte(const Color* before, const Color* effected,
                            Color* output, const float* matte,
                            std::size_t count) {
    if (!matte) {
        std::copy(effected, effected + count, output);
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const float t = matte[i];
        output[i] = {
            before[i].r + (effected[i].r - before[i].r) * t,
            before[i].g + (effected[i].g - before[i].g) * t,
            before[i].b + (effected[i].b - before[i].b) * t,
            before[i].a
        };
    }
}

std::vector<EffectDescriptor> effect_builtin_descriptors() {
    return {
        {"effect.gaussian_blur", "Gaussian Blur", EffectCategory::blur,
         EffectType::blur, 1.0f, 8.0f, 0.0f, 0.0f, 1.0f, 0.0f, 64.0f},
        {"effect.sharpen", "Sharpen", EffectCategory::detail,
         EffectType::sharpen, 0.35f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 8.0f},
        {"effect.glow", "Glow", EffectCategory::light,
         EffectType::glow, 0.5f, 10.0f, 0.0f, 0.0f, 1.0f, 0.0f, 64.0f},
        {"effect.lens_distortion", "Lens Distortion", EffectCategory::lens,
         EffectType::lens_distortion, 0.15f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 4.0f},
        {"effect.noise", "Noise", EffectCategory::stylize,
         EffectType::noise, 0.08f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {"effect.film_grain", "Film Grain", EffectCategory::stylize,
         EffectType::film_grain, 0.08f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {"effect.chromatic_aberration", "Chromatic Aberration", EffectCategory::lens,
         EffectType::chromatic_aberration, 1.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 32.0f},
        {"effect.vignette", "Vignette", EffectCategory::light,
         EffectType::vignette, 0.4f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {"effect.motion_blur", "Motion Blur", EffectCategory::motion,
         EffectType::motion_blur, 1.0f, 8.0f, 0.0f, 0.0f, 1.0f, 0.0f, 64.0f,
         true}
    };
}

} // namespace

bool validate_effect_descriptor(const EffectDescriptor& descriptor,
                                std::string& diagnostic) noexcept {
    diagnostic.clear();
    if (!effect_valid_id(descriptor.id) || descriptor.name.empty()) {
        diagnostic = "invalid effect identity";
        return false;
    }
    const float values[] = {descriptor.default_amount, descriptor.default_radius,
                            descriptor.default_angle, descriptor.minimum_amount,
                            descriptor.maximum_amount, descriptor.minimum_radius,
                            descriptor.maximum_radius};
    for (float value : values) {
        if (!std::isfinite(value)) {
            diagnostic = "effect descriptor contains non-finite value";
            return false;
        }
    }
    if (descriptor.minimum_amount > descriptor.maximum_amount ||
        descriptor.default_amount < descriptor.minimum_amount ||
        descriptor.default_amount > descriptor.maximum_amount ||
        descriptor.minimum_radius > descriptor.maximum_radius ||
        descriptor.default_radius < descriptor.minimum_radius ||
        descriptor.default_radius > descriptor.maximum_radius) {
        diagnostic = "effect descriptor range is invalid";
        return false;
    }
    if (!descriptor.preserves_alpha || !descriptor.deterministic) {
        diagnostic = "built-in production effects must preserve alpha and be deterministic";
        return false;
    }
    return true;
}

EffectRegistry::EffectRegistry() {
    for (auto descriptor : effect_builtin_descriptors()) {
        std::string diagnostic;
        register_effect(std::move(descriptor), &diagnostic);
    }
}

bool EffectRegistry::register_effect(EffectDescriptor descriptor,
                                     std::string* diagnostic) {
    std::string local;
    if (!validate_effect_descriptor(descriptor, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    if (find(descriptor.id)) {
        if (diagnostic) *diagnostic = "duplicate effect id";
        return false;
    }
    effects_.push_back(std::move(descriptor));
    if (diagnostic) diagnostic->clear();
    return true;
}

const EffectDescriptor* EffectRegistry::find(std::string_view id) const noexcept {
    const auto it = std::find_if(effects_.begin(), effects_.end(),
                                 [&](const auto& effect) { return effect.id == id; });
    return it == effects_.end() ? nullptr : &*it;
}

std::vector<const EffectDescriptor*> EffectRegistry::category(EffectCategory category_value) const {
    std::vector<const EffectDescriptor*> result;
    for (const auto& effect : effects_) {
        if (effect.category == category_value) result.push_back(&effect);
    }
    return result;
}

const std::vector<EffectDescriptor>& EffectRegistry::effects() const noexcept {
    return effects_;
}

bool validate_effect_instance(const EffectRegistry& registry,
                              const EffectInstance& instance,
                              std::string& diagnostic) noexcept {
    diagnostic.clear();
    const auto* descriptor = registry.find(instance.effect_id);
    if (!descriptor) {
        diagnostic = "unknown effect id";
        return false;
    }
    if (!std::isfinite(instance.amount) || !std::isfinite(instance.radius) ||
        !std::isfinite(instance.angle) ||
        instance.amount < descriptor->minimum_amount ||
        instance.amount > descriptor->maximum_amount ||
        instance.radius < descriptor->minimum_radius ||
        instance.radius > descriptor->maximum_radius) {
        diagnostic = "effect parameter out of range";
        return false;
    }
    return true;
}

bool EffectStack::add(EffectInstance instance) {
    if (!effect_valid_id(instance.effect_id) ||
        !std::isfinite(instance.amount) || !std::isfinite(instance.radius) ||
        !std::isfinite(instance.angle)) return false;
    entries_.push_back(std::move(instance));
    return true;
}

bool EffectStack::remove(std::size_t index) noexcept {
    if (index >= entries_.size()) return false;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool EffectStack::move(std::size_t from, std::size_t to) noexcept {
    if (from >= entries_.size() || to >= entries_.size()) return false;
    if (from == to) return true;
    auto value = std::move(entries_[from]);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(from));
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(to), std::move(value));
    return true;
}

void EffectStack::clear() noexcept { entries_.clear(); }
const std::vector<EffectInstance>& EffectStack::entries() const noexcept { return entries_; }

std::string EffectStack::serialize() const {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "digitor-effects-v1\n" << entries_.size() << '\n';
    stream << std::setprecision(9);
    for (const auto& entry : entries_) {
        stream << entry.effect_id << '\t' << entry.amount << '\t' << entry.radius
               << '\t' << entry.angle << '\t' << entry.seed << '\t'
               << (entry.enabled ? 1 : 0) << '\n';
    }
    return stream.str();
}

std::optional<EffectStack> EffectStack::deserialize(std::string_view text) {
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(stream, line) || line != "digitor-effects-v1") return std::nullopt;
    if (!std::getline(stream, line)) return std::nullopt;
    std::size_t count{};
    const auto count_result = std::from_chars(line.data(), line.data() + line.size(), count);
    if (count_result.ec != std::errc{} || count_result.ptr != line.data() + line.size() ||
        count > 1024) return std::nullopt;

    EffectStack result;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::getline(stream, line)) return std::nullopt;
        std::vector<std::string_view> fields;
        std::size_t start = 0;
        for (;;) {
            const std::size_t tab = line.find('\t', start);
            fields.push_back(std::string_view(line).substr(start,
                             tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (fields.size() != 6 || !effect_valid_id(fields[0])) return std::nullopt;
        EffectInstance instance;
        instance.effect_id = std::string(fields[0]);
        if (!effect_parse_float(fields[1], instance.amount) ||
            !effect_parse_float(fields[2], instance.radius) ||
            !effect_parse_float(fields[3], instance.angle) ||
            !effect_parse_u64(fields[4], instance.seed) ||
            (fields[5] != "0" && fields[5] != "1")) return std::nullopt;
        instance.enabled = fields[5] == "1";
        if (!result.add(std::move(instance))) return std::nullopt;
    }
    if (std::getline(stream, line) && !line.empty()) return std::nullopt;
    return result;
}

bool apply_effect_stack_cpu(const Color* input, Color* output,
                            std::uint32_t width, std::uint32_t height,
                            const EffectRegistry& registry, const EffectStack& stack,
                            EffectQuality quality, const float* matte,
                            std::size_t matte_count, std::string* diagnostic) {
    std::string local;
    if (!effect_validate_frame(input, output, width, height, matte, matte_count, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<Color> current(input, input + count);
    std::vector<Color> effected(count);
    std::vector<Color> composited(count);

    for (const auto& instance : stack.entries()) {
        if (!instance.enabled) continue;
        if (!validate_effect_instance(registry, instance, local)) {
            if (diagnostic) *diagnostic = local;
            return false;
        }
        const auto* descriptor = registry.find(instance.effect_id);
        CommandBuffer commands;
        CommandEncoder encoder(commands);
        apply_effect_gpu(encoder, current.data(), effected.data(), width, height,
                         effect_settings(*descriptor, instance, quality));
        encoder.finish();
        CommandQueue queue;
        queue.submit(commands);
        effect_composite_matte(current.data(), effected.data(), composited.data(),
                               matte, count);
        current.swap(composited);
    }
    std::copy(current.begin(), current.end(), output);
    if (diagnostic) diagnostic->clear();
    return true;
}

bool apply_effect_stack_gpu(CommandEncoder& encoder, const Color* input, Color* output,
                            std::uint32_t width, std::uint32_t height,
                            const EffectRegistry& registry, const EffectStack& stack,
                            EffectQuality quality, const float* matte,
                            std::size_t matte_count, std::string* diagnostic) {
    std::string local;
    if (!effect_validate_frame(input, output, width, height, matte, matte_count, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    for (const auto& instance : stack.entries()) {
        if (!instance.enabled) continue;
        if (!validate_effect_instance(registry, instance, local)) {
            if (diagnostic) *diagnostic = local;
            return false;
        }
    }
    encoder.dispatch([=, &registry, &stack] {
        std::string ignored;
        if (!apply_effect_stack_cpu(input, output, width, height, registry, stack,
                                    quality, matte, matte_count, &ignored)) {
            throw std::runtime_error(ignored.empty() ? "effect stack failed" : ignored);
        }
    });
    if (diagnostic) diagnostic->clear();
    return true;
}

} // namespace digitor
