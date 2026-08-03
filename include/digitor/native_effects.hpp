#pragma once

#include "digitor/effect_system.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class NativeEffectBackend : std::uint32_t {
    d3d12,
    vulkan,
    metal,
    gles
};

enum class NativeEffectFormat : std::uint32_t {
    rgba8_unorm,
    rgba16_float,
    bgra8_unorm,
    nv12,
    p010
};

struct NativeEffectSurface {
    std::uint64_t texture_handle{};
    std::uint64_t device_identity{};
    std::uint32_t width{};
    std::uint32_t height{};
    NativeEffectFormat format{NativeEffectFormat::rgba8_unorm};
    bool engine_owned{};
    bool external_memory{};
    bool cpu_mappable{};
};

struct NativeEffectPass {
    EffectInstance effect;
    EffectQuality quality{EffectQuality::preview};
    std::uint32_t pass_index{};
    std::uint32_t pass_count{1};
    NativeEffectSurface input;
    NativeEffectSurface output;
};

struct NativeEffectTelemetry {
    std::uint64_t submitted_passes{};
    std::uint64_t transient_allocations{};
    std::uint64_t external_surface_imports{};
    std::uint64_t cpu_readbacks{};
    std::uint64_t cpu_reuploads{};
    std::uint64_t fallback_dispatches{};
};

using NativeEffectPassCount = std::function<std::uint32_t(
    const EffectDescriptor&, const EffectInstance&, EffectQuality)>;
using NativeEffectAllocateSurface = std::function<bool(
    const NativeEffectSurface& prototype, NativeEffectSurface& output,
    std::string& diagnostic)>;
using NativeEffectReleaseSurface = std::function<void(
    const NativeEffectSurface& surface)>;
using NativeEffectRecordPass = std::function<bool(
    const NativeEffectPass& pass, std::string& diagnostic)>;
using NativeEffectSubmit = std::function<bool(std::string& diagnostic)>;

struct NativeEffectBackendProvider {
    NativeEffectBackend backend{NativeEffectBackend::d3d12};
    std::uint64_t device_identity{};
    bool supports_external_memory{};
    bool supports_external_synchronization{};
    bool supports_hdr{};
    NativeEffectPassCount pass_count;
    NativeEffectAllocateSurface allocate_transient;
    NativeEffectReleaseSurface release_transient;
    NativeEffectRecordPass record_pass;
    NativeEffectSubmit submit;
};

bool validate_native_effect_provider(const NativeEffectBackendProvider&,
                                     std::string& diagnostic) noexcept;
bool validate_native_effect_surface(const NativeEffectSurface&,
                                    std::uint64_t expected_device,
                                    std::string& diagnostic) noexcept;

class NativeEffectRuntime {
public:
    explicit NativeEffectRuntime(NativeEffectBackendProvider provider);

    bool execute(const EffectRegistry&, const EffectStack&, EffectQuality,
                 const NativeEffectSurface& input,
                 const NativeEffectSurface& output,
                 std::string* diagnostic = nullptr);

    const NativeEffectBackendProvider& provider() const noexcept;
    const NativeEffectTelemetry& telemetry() const noexcept;
    void reset_telemetry() noexcept;

private:
    NativeEffectBackendProvider provider_;
    NativeEffectTelemetry telemetry_;
};

} // namespace digitor
