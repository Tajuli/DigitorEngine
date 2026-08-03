#pragma once

#include "digitor/effects.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class EffectCategory {
    blur,
    detail,
    light,
    lens,
    stylize,
    motion
};

enum class EffectQuality {
    preview,
    balanced,
    export_quality
};

struct EffectDescriptor {
    std::string id;
    std::string name;
    EffectCategory category{EffectCategory::stylize};
    EffectType type{EffectType::blur};
    float default_amount{1.0f};
    float default_radius{1.0f};
    float default_angle{};
    float minimum_amount{};
    float maximum_amount{1.0f};
    float minimum_radius{};
    float maximum_radius{64.0f};
    bool temporal{};
    bool supports_hdr{true};
    bool preserves_alpha{true};
    bool deterministic{true};
};

struct EffectInstance {
    std::string effect_id;
    float amount{1.0f};
    float radius{1.0f};
    float angle{};
    std::uint64_t seed{};
    bool enabled{true};
};

class EffectRegistry {
public:
    EffectRegistry();

    bool register_effect(EffectDescriptor descriptor,
                         std::string* diagnostic = nullptr);
    const EffectDescriptor* find(std::string_view id) const noexcept;
    std::vector<const EffectDescriptor*> category(EffectCategory) const;
    const std::vector<EffectDescriptor>& effects() const noexcept;

private:
    std::vector<EffectDescriptor> effects_;
};

class EffectStack {
public:
    bool add(EffectInstance instance);
    bool remove(std::size_t index) noexcept;
    bool move(std::size_t from, std::size_t to) noexcept;
    void clear() noexcept;

    const std::vector<EffectInstance>& entries() const noexcept;
    std::string serialize() const;
    static std::optional<EffectStack> deserialize(std::string_view);

private:
    std::vector<EffectInstance> entries_;
};

bool validate_effect_descriptor(const EffectDescriptor&,
                                std::string& diagnostic) noexcept;
bool validate_effect_instance(const EffectRegistry&, const EffectInstance&,
                              std::string& diagnostic) noexcept;

bool apply_effect_stack_cpu(const Color* input, Color* output,
                            std::uint32_t width, std::uint32_t height,
                            const EffectRegistry&, const EffectStack&,
                            EffectQuality quality,
                            const float* matte = nullptr,
                            std::size_t matte_count = 0,
                            std::string* diagnostic = nullptr);

bool apply_effect_stack_gpu(CommandEncoder&, const Color* input, Color* output,
                            std::uint32_t width, std::uint32_t height,
                            const EffectRegistry&, const EffectStack&,
                            EffectQuality quality,
                            const float* matte = nullptr,
                            std::size_t matte_count = 0,
                            std::string* diagnostic = nullptr);

} // namespace digitor
