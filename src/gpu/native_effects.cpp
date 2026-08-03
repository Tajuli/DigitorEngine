#include "digitor/native_effects.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace digitor {
namespace {

bool native_effect_rgb_format(NativeEffectFormat format) noexcept {
    return format == NativeEffectFormat::rgba8_unorm ||
           format == NativeEffectFormat::rgba16_float ||
           format == NativeEffectFormat::bgra8_unorm;
}

void native_effect_release_all(const NativeEffectBackendProvider& provider,
                               std::vector<NativeEffectSurface>& surfaces) noexcept {
    if (provider.release_transient) {
        for (auto it = surfaces.rbegin(); it != surfaces.rend(); ++it)
            provider.release_transient(*it);
    }
    surfaces.clear();
}

} // namespace

bool validate_native_effect_provider(const NativeEffectBackendProvider& provider,
                                     std::string& diagnostic) noexcept {
    diagnostic.clear();
    if (!provider.device_identity) {
        diagnostic = "native effect provider has no device identity";
        return false;
    }
    if (!provider.supports_external_memory ||
        !provider.supports_external_synchronization) {
        diagnostic = "native effect provider lacks zero-copy interop";
        return false;
    }
    if (!provider.pass_count || !provider.allocate_transient ||
        !provider.release_transient || !provider.record_pass || !provider.submit) {
        diagnostic = "native effect provider callbacks are incomplete";
        return false;
    }
    return true;
}

bool validate_native_effect_surface(const NativeEffectSurface& surface,
                                    std::uint64_t expected_device,
                                    std::string& diagnostic) noexcept {
    diagnostic.clear();
    if (!surface.texture_handle || !surface.device_identity ||
        !surface.width || !surface.height) {
        diagnostic = "native effect surface is incomplete";
        return false;
    }
    if (surface.device_identity != expected_device) {
        diagnostic = "native effect surface device identity mismatch";
        return false;
    }
    if (!native_effect_rgb_format(surface.format)) {
        diagnostic = "native effects require an RGB working surface";
        return false;
    }
    if (surface.cpu_mappable) {
        diagnostic = "CPU-mappable surfaces are forbidden in native effects";
        return false;
    }
    if (!surface.engine_owned && !surface.external_memory) {
        diagnostic = "external native effect surface lacks external-memory identity";
        return false;
    }
    return true;
}

NativeEffectRuntime::NativeEffectRuntime(NativeEffectBackendProvider provider)
    : provider_(std::move(provider)) {
    std::string diagnostic;
    if (!validate_native_effect_provider(provider_, diagnostic))
        throw std::invalid_argument(diagnostic);
}

bool NativeEffectRuntime::execute(const EffectRegistry& registry,
                                  const EffectStack& stack,
                                  EffectQuality quality,
                                  const NativeEffectSurface& input,
                                  const NativeEffectSurface& output,
                                  std::string* diagnostic) {
    std::string local;
    if (!validate_native_effect_surface(input, provider_.device_identity, local) ||
        !validate_native_effect_surface(output, provider_.device_identity, local)) {
        if (diagnostic) *diagnostic = local;
        return false;
    }
    if (input.width != output.width || input.height != output.height ||
        input.format != output.format) {
        if (diagnostic) *diagnostic = "native effect input/output geometry mismatch";
        return false;
    }
    if (input.texture_handle == output.texture_handle) {
        if (diagnostic) *diagnostic = "native effect input/output aliasing is forbidden";
        return false;
    }
    if (input.format == NativeEffectFormat::rgba16_float && !provider_.supports_hdr) {
        if (diagnostic) *diagnostic = "native effect provider does not support HDR surfaces";
        return false;
    }

    std::vector<const EffectInstance*> enabled;
    enabled.reserve(stack.entries().size());
    for (const auto& instance : stack.entries()) {
        if (!instance.enabled) continue;
        if (!validate_effect_instance(registry, instance, local)) {
            if (diagnostic) *diagnostic = local;
            return false;
        }
        enabled.push_back(&instance);
    }
    if (enabled.empty()) {
        if (diagnostic) *diagnostic = "native effect stack contains no enabled effects";
        return false;
    }

    std::vector<NativeEffectSurface> transients;
    NativeEffectSurface current = input;
    for (std::size_t effect_index = 0; effect_index < enabled.size(); ++effect_index) {
        const auto& instance = *enabled[effect_index];
        const auto* descriptor = registry.find(instance.effect_id);
        if (!descriptor) {
            native_effect_release_all(provider_, transients);
            if (diagnostic) *diagnostic = "native effect descriptor disappeared";
            return false;
        }
        const std::uint32_t pass_count = provider_.pass_count(*descriptor, instance, quality);
        if (!pass_count || pass_count > 32) {
            native_effect_release_all(provider_, transients);
            if (diagnostic) *diagnostic = "native effect pass count is invalid";
            return false;
        }

        for (std::uint32_t pass_index = 0; pass_index < pass_count; ++pass_index) {
            const bool final_pass = effect_index + 1 == enabled.size() &&
                                    pass_index + 1 == pass_count;
            NativeEffectSurface target;
            if (final_pass) {
                target = output;
            } else {
                NativeEffectSurface prototype = input;
                prototype.engine_owned = true;
                prototype.external_memory = false;
                prototype.cpu_mappable = false;
                prototype.texture_handle = 0;
                if (!provider_.allocate_transient(prototype, target, local) ||
                    !validate_native_effect_surface(target, provider_.device_identity, local) ||
                    !target.engine_owned || target.width != input.width ||
                    target.height != input.height || target.format != input.format ||
                    target.texture_handle == current.texture_handle) {
                    native_effect_release_all(provider_, transients);
                    if (diagnostic) *diagnostic = local.empty()
                        ? "native effect transient allocation failed" : local;
                    return false;
                }
                transients.push_back(target);
                ++telemetry_.transient_allocations;
            }

            NativeEffectPass pass;
            pass.effect = instance;
            pass.quality = quality;
            pass.pass_index = pass_index;
            pass.pass_count = pass_count;
            pass.input = current;
            pass.output = target;
            if (!provider_.record_pass(pass, local)) {
                native_effect_release_all(provider_, transients);
                if (diagnostic) *diagnostic = local.empty()
                    ? "native effect pass recording failed" : local;
                return false;
            }
            ++telemetry_.submitted_passes;
            current = target;
        }
    }

    if (!provider_.submit(local)) {
        native_effect_release_all(provider_, transients);
        if (diagnostic) *diagnostic = local.empty()
            ? "native effect submission failed" : local;
        return false;
    }

    telemetry_.external_surface_imports +=
        static_cast<std::uint64_t>(!input.engine_owned) +
        static_cast<std::uint64_t>(!output.engine_owned);
    native_effect_release_all(provider_, transients);
    if (diagnostic) diagnostic->clear();
    return true;
}

const NativeEffectBackendProvider& NativeEffectRuntime::provider() const noexcept {
    return provider_;
}

const NativeEffectTelemetry& NativeEffectRuntime::telemetry() const noexcept {
    return telemetry_;
}

void NativeEffectRuntime::reset_telemetry() noexcept {
    telemetry_ = {};
}

} // namespace digitor
