#include "digitor/beauty.hpp"
#include "digitor/plugin.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace digitor {
namespace {

float value(const PluginInstance& instance, const char* id, float fallback) {
    const auto it = instance.values.find(id);
    return it == instance.values.end() ? fallback : it->second;
}

struct BeautyRuntime {
    BeautyProcessor& processor(std::uint64_t stream_id) {
        std::lock_guard<std::mutex> lock(mutex);
        auto& slot = streams[stream_id];
        if (!slot) slot = std::make_unique<BeautyProcessor>();
        return *slot;
    }
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::unique_ptr<BeautyProcessor>> streams;
};

const char* beauty_id(BeautyKind kind) {
    switch (kind) {
        case BeautyKind::skin_brighten: return "beauty.skin_brighten";
        case BeautyKind::skin_smooth: return "beauty.skin_smooth";
        case BeautyKind::even_skin_tone: return "beauty.even_skin_tone";
        case BeautyKind::blemish_reduction: return "beauty.blemish_reduction";
    }
    return "beauty.invalid";
}

const char* beauty_name(BeautyKind kind) {
    switch (kind) {
        case BeautyKind::skin_brighten: return "Skin Brighten";
        case BeautyKind::skin_smooth: return "Skin Smooth";
        case BeautyKind::even_skin_tone: return "Even Skin Tone";
        case BeautyKind::blemish_reduction: return "Blemish Reduction";
    }
    return "Beauty";
}

BeautySettings settings(BeautyKind kind, const PluginInstance& instance) {
    BeautySettings result;
    result.kind = kind;
    result.amount = value(instance, "amount", 0.5f);
    result.detail_protection = value(instance, "detail_protection", 0.7f);
    result.edge_protection = value(instance, "edge_protection", 0.8f);
    result.temporal_stability = value(instance, "temporal_stability", 0.75f);
    result.highlight_protection = value(instance, "highlight_protection", 0.8f);
    return result;
}

BeautyFrameContext frame_context(const PluginExecutionContext& context) {
    BeautyFrameContext result;
    result.width = context.width;
    result.height = context.height;
    result.frame = context.frame;
    result.stream_id = context.stream_id;
    result.skin_matte = context.skin_matte;
    result.skin_matte_count = context.skin_matte_count;
    result.scene_cut = context.scene_cut;
    result.hdr = context.hdr;
    return result;
}

} // namespace

PluginDefinition make_beauty_plugin(BeautyKind kind, std::string vendor) {
    PluginDefinition result;
    result.descriptor.id = beauty_id(kind);
    result.descriptor.name = beauty_name(kind);
    result.descriptor.vendor = std::move(vendor);
    result.descriptor.version = "1.0.0";
    result.descriptor.minimum_engine_version = "5.0.0";
    result.descriptor.kind = PluginKind::filter;
    result.descriptor.trust = PluginTrust::trusted_native;
    result.descriptor.backend_flags = plugin_backend_all;
    result.descriptor.supports_sdr = true;
    result.descriptor.supports_hdr = true;
    result.descriptor.preserves_alpha = true;
    result.descriptor.deterministic = true;
    result.descriptor.temporal = true;
    result.descriptor.parameters = {
        {"amount", "Amount", PluginParameterType::floating, 0.0f, 1.0f, 0.5f, {}, true},
        {"detail_protection", "Detail Protection", PluginParameterType::floating,
         0.0f, 1.0f, 0.7f, {}, true},
        {"edge_protection", "Edge Protection", PluginParameterType::floating,
         0.0f, 1.0f, 0.8f, {}, true},
        {"temporal_stability", "Temporal Stability", PluginParameterType::floating,
         0.0f, 1.0f, 0.75f, {}, true},
        {"highlight_protection", "Highlight Protection", PluginParameterType::floating,
         0.0f, 1.0f, 0.8f, {}, true}
    };

    auto runtime = std::make_shared<BeautyRuntime>();
    result.cpu_processor = [runtime, kind](const PluginExecutionContext& context,
                                           const PluginInstance& instance,
                                           const Color* input, Color* output,
                                           std::size_t count) {
        return runtime->processor(context.stream_id).process_cpu(
            frame_context(context), settings(kind, instance), input, output, count);
    };
    result.gpu_recorder = [runtime, kind](CommandEncoder& encoder,
                                          const PluginExecutionContext& context,
                                          const PluginInstance& instance,
                                          const Color* input, Color* output,
                                          std::size_t count) {
        return runtime->processor(context.stream_id).process_gpu(
            encoder, frame_context(context), settings(kind, instance), input, output, count);
    };
    return result;
}

std::vector<PluginDefinition> make_builtin_beauty_plugins(std::string vendor) {
    std::vector<PluginDefinition> result;
    result.reserve(4);
    result.push_back(make_beauty_plugin(BeautyKind::skin_brighten, vendor));
    result.push_back(make_beauty_plugin(BeautyKind::skin_smooth, vendor));
    result.push_back(make_beauty_plugin(BeautyKind::even_skin_tone, vendor));
    result.push_back(make_beauty_plugin(BeautyKind::blemish_reduction, std::move(vendor)));
    return result;
}

} // namespace digitor
