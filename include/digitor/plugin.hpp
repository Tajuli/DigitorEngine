#pragma once

#include "digitor/color.hpp"
#include "digitor/effects.hpp"
#include "digitor/filter.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

inline constexpr std::uint32_t plugin_sdk_abi_version = 1;

enum class BeautyKind;
enum class PluginKind { filter, video_effect };
enum class PluginTrust { sandboxed_shader, trusted_native };
enum class PluginParameterType { floating, integer, boolean, enumeration, color };

enum PluginBackendFlags : std::uint32_t {
    plugin_backend_none = 0,
    plugin_backend_vulkan = 1u << 0,
    plugin_backend_d3d12 = 1u << 1,
    plugin_backend_metal = 1u << 2,
    plugin_backend_gles = 1u << 3,
    plugin_backend_all = plugin_backend_vulkan | plugin_backend_d3d12 |
                         plugin_backend_metal | plugin_backend_gles
};

struct PluginParameterDescriptor {
    std::string id;
    std::string name;
    PluginParameterType type{PluginParameterType::floating};
    float minimum{};
    float maximum{1.0f};
    float default_value{};
    std::vector<std::string> enum_values;
    bool keyframeable{true};
};

struct PluginDescriptor {
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::string minimum_engine_version;
    PluginKind kind{PluginKind::filter};
    PluginTrust trust{PluginTrust::sandboxed_shader};
    std::uint32_t abi_version{plugin_sdk_abi_version};
    std::uint32_t backend_flags{plugin_backend_all};
    bool supports_sdr{true};
    bool supports_hdr{true};
    bool preserves_alpha{true};
    bool deterministic{true};
    bool temporal{};
    bool requires_network{};
    bool requires_filesystem{};
    std::vector<PluginParameterDescriptor> parameters;
};

struct PluginInstance {
    std::string plugin_id;
    std::unordered_map<std::string, float> values;
    bool enabled{true};
};

struct PluginExecutionContext {
    std::uint32_t width{};
    std::uint32_t height{};
    std::int64_t frame{};
    bool hdr{};
    std::uint32_t backend_flag{plugin_backend_none};

    // Optional engine-owned, refined R32F skin matte. Beauty plugins use a
    // conservative chroma fallback when absent, but production hosts should
    // provide a face-aware matte for eyes/lips/hair/background protection.
    const float* skin_matte{};
    std::size_t skin_matte_count{};
    std::uint64_t stream_id{};
    bool scene_cut{};
};

using PluginCpuProcessor = std::function<bool(
    const PluginExecutionContext&, const PluginInstance&,
    const Color*, Color*, std::size_t)>;
using PluginGpuRecorder = std::function<bool(
    CommandEncoder&, const PluginExecutionContext&, const PluginInstance&,
    const Color*, Color*, std::size_t)>;

struct PluginDefinition {
    PluginDescriptor descriptor;
    PluginCpuProcessor cpu_processor;
    PluginGpuRecorder gpu_recorder;
};

class PluginRegistry {
public:
    bool register_plugin(PluginDefinition definition, std::string* diagnostic = nullptr);
    bool unregister_plugin(std::string_view id) noexcept;
    const PluginDefinition* find(std::string_view id) const noexcept;
    std::vector<const PluginDefinition*> plugins(PluginKind kind) const;
    const std::vector<PluginDefinition>& all() const noexcept;

private:
    std::vector<PluginDefinition> plugins_;
};

bool validate_plugin_descriptor(const PluginDescriptor&, std::string& diagnostic) noexcept;
bool validate_plugin_instance(const PluginDefinition&, const PluginInstance&,
                              const PluginExecutionContext&, std::string& diagnostic) noexcept;

bool execute_plugin_cpu(const PluginRegistry&, const PluginExecutionContext&,
                        const PluginInstance&, const Color*, Color*, std::size_t,
                        std::string* diagnostic = nullptr);
bool execute_plugin_gpu(const PluginRegistry&, CommandEncoder&,
                        const PluginExecutionContext&, const PluginInstance&,
                        const Color*, Color*, std::size_t,
                        std::string* diagnostic = nullptr);

std::string serialize_plugin_instance(const PluginInstance&);
std::optional<PluginInstance> deserialize_plugin_instance(std::string_view);

PluginDefinition make_filter_plugin(FilterPreset preset,
                                    std::string vendor = "Digitor");
PluginDefinition make_effect_plugin(std::string id, std::string name,
                                    EffectType effect,
                                    std::string vendor = "Digitor");
PluginDefinition make_beauty_plugin(BeautyKind kind,
                                    std::string vendor = "Digitor");
std::vector<PluginDefinition> make_builtin_beauty_plugins(
                                    std::string vendor = "Digitor");

} // namespace digitor
