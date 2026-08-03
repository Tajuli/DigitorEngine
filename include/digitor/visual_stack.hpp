#pragma once

#include "digitor/effect_system.hpp"
#include "digitor/filter.hpp"
#include "digitor/plugin.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

// Stable project-level ordering: filters -> plugins/beauty -> effects.
// Preview and export must consume the same serialized VisualStack.
class VisualStack {
public:
    FilterStack filters;
    std::vector<PluginInstance> plugins;
    EffectStack effects;

    std::string serialize() const;
    static std::optional<VisualStack> deserialize(std::string_view text);
};

bool validate_visual_stack(const VisualStack&, const FilterRegistry&,
                           const PluginRegistry&, const EffectRegistry&,
                           std::string& diagnostic) noexcept;

} // namespace digitor
