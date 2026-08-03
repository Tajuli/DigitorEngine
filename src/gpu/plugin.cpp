#include "digitor/plugin.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace digitor {
namespace {

bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

float parameter(const PluginInstance& instance, std::string_view id, float fallback) {
    const auto it = instance.values.find(std::string(id));
    return it == instance.values.end() ? fallback : it->second;
}

std::string effect_name(EffectType type) {
    switch (type) {
        case EffectType::blur: return "Blur";
        case EffectType::sharpen: return "Sharpen";
        case EffectType::glow: return "Glow";
        case EffectType::lens_distortion: return "Lens Distortion";
        case EffectType::noise: return "Noise";
        case EffectType::film_grain: return "Film Grain";
        case EffectType::chromatic_aberration: return "Chromatic Aberration";
        case EffectType::vignette: return "Vignette";
        case EffectType::motion_blur: return "Motion Blur";
    }
    return "Effect";
}

} // namespace

bool validate_plugin_descriptor(const PluginDescriptor& descriptor,
                                std::string& diagnostic) noexcept {
    diagnostic.clear();
    if (!valid_id(descriptor.id)) {
        diagnostic = "plugin id must use lowercase ASCII letters, digits, dot, dash, or underscore";
        return false;
    }
    if (descriptor.name.empty() || descriptor.vendor.empty() || descriptor.version.empty()) {
        diagnostic = "plugin name, vendor, and version are required";
        return false;
    }
    if (descriptor.abi_version != plugin_sdk_abi_version) {
        diagnostic = "plugin ABI version mismatch";
        return false;
    }
    if (descriptor.backend_flags == plugin_backend_none) {
        diagnostic = "plugin must support at least one GPU backend";
        return false;
    }
    if (!descriptor.supports_sdr && !descriptor.supports_hdr) {
        diagnostic = "plugin must support SDR or HDR";
        return false;
    }
    if (descriptor.trust == PluginTrust::sandboxed_shader &&
        (descriptor.requires_network || descriptor.requires_filesystem)) {
        diagnostic = "sandboxed plugins cannot request network or filesystem access";
        return false;
    }
    std::vector<std::string> ids;
    for (const auto& p : descriptor.parameters) {
        if (!valid_id(p.id) || p.name.empty() || !std::isfinite(p.minimum) ||
            !std::isfinite(p.maximum) || !std::isfinite(p.default_value) ||
            p.minimum > p.maximum || p.default_value < p.minimum ||
            p.default_value > p.maximum) {
            diagnostic = "invalid plugin parameter descriptor";
            return false;
        }
        if (std::find(ids.begin(), ids.end(), p.id) != ids.end()) {
            diagnostic = "duplicate plugin parameter id";
            return false;
        }
        if (p.type == PluginParameterType::enumeration && p.enum_values.empty()) {
            diagnostic = "enumeration parameter requires values";
            return false;
        }
        ids.push_back(p.id);
    }
    return true;
}

bool validate_plugin_instance(const PluginDefinition& definition,
                              const PluginInstance& instance,
                              const PluginExecutionContext& context,
                              std::string& diagnostic) noexcept {
    diagnostic.clear();
    if (instance.plugin_id != definition.descriptor.id) {
        diagnostic = "plugin instance identity mismatch";
        return false;
    }
    if (!context.width || !context.height || context.backend_flag == plugin_backend_none) {
        diagnostic = "invalid plugin execution context";
        return false;
    }
    if ((definition.descriptor.backend_flags & context.backend_flag) == 0) {
        diagnostic = "plugin does not support selected backend";
        return false;
    }
    if (context.hdr && !definition.descriptor.supports_hdr) {
        diagnostic = "plugin does not support HDR";
        return false;
    }
    if (!context.hdr && !definition.descriptor.supports_sdr) {
        diagnostic = "plugin does not support SDR";
        return false;
    }
    for (const auto& [id, value] : instance.values) {
        const auto it = std::find_if(definition.descriptor.parameters.begin(),
                                     definition.descriptor.parameters.end(),
                                     [&](const auto& p) { return p.id == id; });
        if (it == definition.descriptor.parameters.end()) {
            diagnostic = "unknown plugin parameter";
            return false;
        }
        if (!std::isfinite(value) || value < it->minimum || value > it->maximum) {
            diagnostic = "plugin parameter value out of range";
            return false;
        }
    }
    return true;
}

bool PluginRegistry::register_plugin(PluginDefinition definition,
                                     std::string* diagnostic) {
    std::string local;
    if (!validate_plugin_descriptor(definition.descriptor, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    if (!definition.cpu_processor && !definition.gpu_recorder) {
        if (diagnostic) *diagnostic = "plugin has no execution path";
        return false;
    }
    if (find(definition.descriptor.id)) {
        if (diagnostic) *diagnostic = "duplicate plugin id";
        return false;
    }
    plugins_.push_back(std::move(definition));
    if (diagnostic) diagnostic->clear();
    return true;
}

bool PluginRegistry::unregister_plugin(std::string_view id) noexcept {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [&](const auto& p) { return p.descriptor.id == id; });
    if (it == plugins_.end()) return false;
    plugins_.erase(it);
    return true;
}

const PluginDefinition* PluginRegistry::find(std::string_view id) const noexcept {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [&](const auto& p) { return p.descriptor.id == id; });
    return it == plugins_.end() ? nullptr : &*it;
}

std::vector<const PluginDefinition*> PluginRegistry::plugins(PluginKind kind) const {
    std::vector<const PluginDefinition*> result;
    for (const auto& p : plugins_) if (p.descriptor.kind == kind) result.push_back(&p);
    return result;
}

const std::vector<PluginDefinition>& PluginRegistry::all() const noexcept {
    return plugins_;
}

bool execute_plugin_cpu(const PluginRegistry& registry,
                        const PluginExecutionContext& context,
                        const PluginInstance& instance,
                        const Color* input, Color* output, std::size_t count,
                        std::string* diagnostic) {
    if (!input || !output || count != static_cast<std::size_t>(context.width) * context.height) {
        if (diagnostic) *diagnostic = "invalid plugin image buffers";
        return false;
    }
    const auto* definition = registry.find(instance.plugin_id);
    if (!definition) {
        if (diagnostic) *diagnostic = "plugin not registered";
        return false;
    }
    std::string local;
    if (!validate_plugin_instance(*definition, instance, context, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    if (!instance.enabled) {
        std::copy(input, input + count, output);
        if (diagnostic) diagnostic->clear();
        return true;
    }
    if (!definition->cpu_processor) {
        if (diagnostic) *diagnostic = "plugin has no CPU processor";
        return false;
    }
    const bool ok = definition->cpu_processor(context, instance, input, output, count);
    if (diagnostic) *diagnostic = ok ? "" : "plugin CPU execution failed";
    return ok;
}

bool execute_plugin_gpu(const PluginRegistry& registry, CommandEncoder& encoder,
                        const PluginExecutionContext& context,
                        const PluginInstance& instance,
                        const Color* input, Color* output, std::size_t count,
                        std::string* diagnostic) {
    if (!input || !output || count != static_cast<std::size_t>(context.width) * context.height) {
        if (diagnostic) *diagnostic = "invalid plugin image buffers";
        return false;
    }
    const auto* definition = registry.find(instance.plugin_id);
    if (!definition) {
        if (diagnostic) *diagnostic = "plugin not registered";
        return false;
    }
    std::string local;
    if (!validate_plugin_instance(*definition, instance, context, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    if (!instance.enabled) {
        encoder.dispatch([=] { std::copy(input, input + count, output); });
        if (diagnostic) diagnostic->clear();
        return true;
    }
    if (!definition->gpu_recorder) {
        if (diagnostic) *diagnostic = "plugin has no GPU recorder";
        return false;
    }
    const bool ok = definition->gpu_recorder(encoder, context, instance, input, output, count);
    if (diagnostic) *diagnostic = ok ? "" : "plugin GPU recording failed";
    return ok;
}

std::string serialize_plugin_instance(const PluginInstance& instance) {
    std::ostringstream out;
    out << "digitor-plugin-v1\n" << instance.plugin_id << '\n'
        << (instance.enabled ? 1 : 0) << '\n' << instance.values.size() << '\n';
    for (const auto& [id, value] : instance.values) out << id << '=' << value << '\n';
    return out.str();
}

std::optional<PluginInstance> deserialize_plugin_instance(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line) || line != "digitor-plugin-v1") return std::nullopt;
    PluginInstance result;
    if (!std::getline(in, result.plugin_id) || !valid_id(result.plugin_id)) return std::nullopt;
    if (!std::getline(in, line) || (line != "0" && line != "1")) return std::nullopt;
    result.enabled = line == "1";
    std::size_t count{};
    if (!std::getline(in, line)) return std::nullopt;
    const auto parsed = std::from_chars(line.data(), line.data() + line.size(), count);
    if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || count > 256)
        return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::getline(in, line)) return std::nullopt;
        const auto split = line.find('=');
        if (split == std::string::npos) return std::nullopt;
        const std::string id = line.substr(0, split);
        if (!valid_id(id)) return std::nullopt;
        float value{};
        const auto number = std::string_view(line).substr(split + 1);
        const auto value_result = std::from_chars(number.data(), number.data() + number.size(), value);
        if (value_result.ec != std::errc{} || value_result.ptr != number.data() + number.size() ||
            !std::isfinite(value)) return std::nullopt;
        result.values.emplace(id, value);
    }
    if (std::getline(in, line) && !line.empty()) return std::nullopt;
    return result;
}

PluginDefinition make_filter_plugin(FilterPreset preset, std::string vendor) {
    PluginDefinition result;
    result.descriptor.id = "filter." + preset.id;
    result.descriptor.name = preset.name;
    result.descriptor.vendor = std::move(vendor);
    result.descriptor.version = "1.0.0";
    result.descriptor.minimum_engine_version = "5.0.0";
    result.descriptor.kind = PluginKind::filter;
    result.descriptor.trust = PluginTrust::sandboxed_shader;
    result.descriptor.supports_hdr = preset.hdr_safe;
    result.descriptor.parameters.push_back({"intensity", "Intensity",
        PluginParameterType::floating, 0.0f, 1.0f, 1.0f, {}, true});
    result.cpu_processor = [preset](const PluginExecutionContext&, const PluginInstance& instance,
                                    const Color* input, Color* output, std::size_t count) {
        apply_filter_cpu(input, output, count, preset, parameter(instance, "intensity", 1.0f));
        return true;
    };
    result.gpu_recorder = [preset](CommandEncoder& encoder, const PluginExecutionContext&,
                                   const PluginInstance& instance,
                                   const Color* input, Color* output, std::size_t count) {
        apply_filter_gpu(encoder, input, output, count, preset,
                         parameter(instance, "intensity", 1.0f));
        return true;
    };
    return result;
}

PluginDefinition make_effect_plugin(std::string id, std::string name,
                                    EffectType effect, std::string vendor) {
    PluginDefinition result;
    result.descriptor.id = "effect." + std::move(id);
    result.descriptor.name = name.empty() ? effect_name(effect) : std::move(name);
    result.descriptor.vendor = std::move(vendor);
    result.descriptor.version = "1.0.0";
    result.descriptor.minimum_engine_version = "5.0.0";
    result.descriptor.kind = PluginKind::video_effect;
    result.descriptor.trust = PluginTrust::trusted_native;
    result.descriptor.parameters = {
        {"amount", "Amount", PluginParameterType::floating, 0.0f, 4.0f, 1.0f, {}, true},
        {"radius", "Radius", PluginParameterType::floating, 0.0f, 64.0f, 1.0f, {}, true},
        {"angle", "Angle", PluginParameterType::floating, -6.283185f, 6.283185f, 0.0f, {}, true}
    };
    result.gpu_recorder = [effect](CommandEncoder& encoder, const PluginExecutionContext& context,
                                   const PluginInstance& instance,
                                   const Color* input, Color* output, std::size_t) {
        EffectSettings settings;
        settings.type = effect;
        settings.amount = parameter(instance, "amount", 1.0f);
        settings.radius = parameter(instance, "radius", 1.0f);
        settings.angle = parameter(instance, "angle", 0.0f);
        settings.seed = static_cast<std::uint64_t>(context.frame);
        apply_effect_gpu(encoder, input, output, context.width, context.height, settings);
        return true;
    };
    return result;
}

} // namespace digitor
