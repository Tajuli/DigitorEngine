#include "digitor/visual_stack.hpp"

#include <charconv>
#include <cmath>
#include <sstream>

namespace digitor {
namespace {

bool visual_read_size(std::string_view text, std::size_t& cursor, std::size_t& value) {
    const auto end = text.find('\n', cursor);
    if (end == std::string_view::npos) return false;
    const auto field = text.substr(cursor, end - cursor);
    const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
    if (result.ec != std::errc{} || result.ptr != field.data() + field.size() ||
        value > 16u * 1024u * 1024u) return false;
    cursor = end + 1;
    return true;
}

bool visual_read_blob(std::string_view text, std::size_t& cursor, std::string_view& blob) {
    std::size_t size{};
    if (!visual_read_size(text, cursor, size) || size > text.size() - cursor) return false;
    blob = text.substr(cursor, size);
    cursor += size;
    if (cursor >= text.size() || text[cursor] != '\n') return false;
    ++cursor;
    return true;
}

void visual_write_blob(std::ostringstream& stream, std::string_view blob) {
    stream << blob.size() << '\n';
    stream.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    stream << '\n';
}

bool visual_validate_plugin_values(const PluginDefinition& definition,
                                   const PluginInstance& instance,
                                   std::string& diagnostic) {
    if (instance.plugin_id != definition.descriptor.id) {
        diagnostic = "visual plugin identity mismatch";
        return false;
    }
    for (const auto& [id, value] : instance.values) {
        if (!std::isfinite(value)) {
            diagnostic = "visual plugin parameter is non-finite";
            return false;
        }
        const PluginParameterDescriptor* parameter = nullptr;
        for (const auto& candidate : definition.descriptor.parameters) {
            if (candidate.id == id) { parameter = &candidate; break; }
        }
        if (!parameter || value < parameter->minimum || value > parameter->maximum) {
            diagnostic = "visual plugin parameter is unknown or out of range";
            return false;
        }
    }
    return true;
}

} // namespace

std::string VisualStack::serialize() const {
    std::ostringstream stream;
    stream << "digitor-visual-stack-v1\n";
    visual_write_blob(stream, filters.serialize());
    stream << plugins.size() << '\n';
    for (const auto& plugin : plugins)
        visual_write_blob(stream, serialize_plugin_instance(plugin));
    visual_write_blob(stream, effects.serialize());
    return stream.str();
}

std::optional<VisualStack> VisualStack::deserialize(std::string_view text) {
    constexpr std::string_view header = "digitor-visual-stack-v1\n";
    if (!text.starts_with(header)) return std::nullopt;
    std::size_t cursor = header.size();
    std::string_view blob;
    if (!visual_read_blob(text, cursor, blob)) return std::nullopt;
    auto filters = FilterStack::deserialize(blob);
    if (!filters) return std::nullopt;

    std::size_t plugin_count{};
    if (!visual_read_size(text, cursor, plugin_count) || plugin_count > 256) return std::nullopt;
    VisualStack result;
    result.filters = std::move(*filters);
    result.plugins.reserve(plugin_count);
    for (std::size_t i = 0; i < plugin_count; ++i) {
        if (!visual_read_blob(text, cursor, blob)) return std::nullopt;
        auto plugin = deserialize_plugin_instance(blob);
        if (!plugin) return std::nullopt;
        result.plugins.push_back(std::move(*plugin));
    }
    if (!visual_read_blob(text, cursor, blob) || cursor != text.size()) return std::nullopt;
    auto effects = EffectStack::deserialize(blob);
    if (!effects) return std::nullopt;
    result.effects = std::move(*effects);
    return result;
}

bool validate_visual_stack(const VisualStack& stack, const FilterRegistry& filters,
                           const PluginRegistry& plugins, const EffectRegistry& effects,
                           std::string& diagnostic) noexcept {
    diagnostic.clear();
    // FilterStack owns its stable IDs and range checks; a serialize/deserialize
    // round-trip additionally rejects malformed in-memory project state.
    const auto filter_roundtrip = FilterStack::deserialize(stack.filters.serialize());
    if (!filter_roundtrip) {
        diagnostic = "invalid visual filter stack";
        return false;
    }
    (void)filters;

    for (const auto& instance : stack.plugins) {
        const auto* definition = plugins.find(instance.plugin_id);
        if (!definition) {
            diagnostic = "unknown visual plugin id";
            return false;
        }
        if (!visual_validate_plugin_values(*definition, instance, diagnostic)) return false;
    }
    for (const auto& instance : stack.effects.entries()) {
        if (!validate_effect_instance(effects, instance, diagnostic)) return false;
    }
    return true;
}

} // namespace digitor
