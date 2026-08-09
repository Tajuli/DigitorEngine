#include "digitor/flutter_production_c_api.h"

#include <cstring>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

struct DigitorFlutterProductionSession {
    std::mutex mutex;
    DigitorFlutterProductionHost host{};
    DigitorNodeGraph* graph{};
    uint64_t graph_revision{};
    uint64_t parameter_revision{};
    DigitorNativeGpuTextureDescriptor current_preview{};
    std::string last_error;
    bool has_current_preview{};
    bool active_operation{};
    bool media_open{};
};

namespace {
constexpr uint32_t kDiagnosticCapacity = 512;
std::mutex g_flutter_sessions_mutex;
std::unordered_set<DigitorFlutterProductionSession*> g_flutter_sessions;
std::mutex g_registered_host_mutex;
std::optional<DigitorFlutterProductionHost> g_registered_host;

bool valid_host(const DigitorFlutterProductionHost* host) noexcept {
    return host &&
           host->struct_size >= sizeof(DigitorFlutterProductionHost) &&
           host->api_version == DIGITOR_FLUTTER_PRODUCTION_HOST_VERSION &&
           host->open_media && host->render_frame && host->export_media &&
           host->query_preview && host->cancel && host->close_media &&
           host->release_texture;
}

bool registered(const DigitorFlutterProductionSession* session) noexcept {
    return session != nullptr &&
           g_flutter_sessions.contains(
               const_cast<DigitorFlutterProductionSession*>(session));
}

DigitorResult validate_graph(DigitorNodeGraph* graph) noexcept {
    if (!graph) return DIGITOR_RESULT_INVALID_ARGUMENT;
    uint64_t required = 0;
    return digitor_node_graph_recipe_identity(graph, nullptr, 0, &required);
}

bool valid_texture(const DigitorNativeGpuTextureDescriptor& value,
                   const DigitorFlutterProductionHost& host,
                   uint32_t width, uint32_t height,
                   int64_t timestamp_us) noexcept {
    if (value.struct_size < sizeof(DigitorNativeGpuTextureDescriptor) ||
        value.api_version != DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION ||
        value.readiness != DIGITOR_NATIVE_TEXTURE_READY ||
        value.native_handle == 0 || value.width == 0 || value.height == 0 ||
        value.generation == 0 ||
        value.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_NONE ||
        value.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_CPU_RGBA8 ||
        value.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
        value.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
        return false;
    }
    if (width != 0 && value.width != width) return false;
    if (height != 0 && value.height != height) return false;
    if (value.timestamp_us != timestamp_us) return false;
    if (host.required_device_identity != 0 &&
        value.device_identity != host.required_device_identity) return false;
    if (host.required_context_identity != 0 &&
        value.context_identity != host.required_context_identity) return false;
    return true;
}

void release_texture(const DigitorFlutterProductionHost& host,
                     DigitorNativeGpuTextureDescriptor& texture) noexcept {
    if (texture.native_handle != 0 && host.release_texture) {
        host.release_texture(host.user_data, &texture);
    }
    texture = {};
}

DigitorResult copy_error(const std::string& value, char* buffer,
                         uint32_t* inout_size) noexcept {
    if (!inout_size) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto required = static_cast<uint32_t>(value.size() + 1);
    if (!buffer) {
        *inout_size = required;
        return DIGITOR_RESULT_OK;
    }
    if (*inout_size < required) {
        *inout_size = required;
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    std::memcpy(buffer, value.c_str(), required);
    *inout_size = required;
    return DIGITOR_RESULT_OK;
}

} // namespace

extern "C" {

DigitorResult digitor_flutter_production_register_host(
    const DigitorFlutterProductionHost* host) {
    if (!valid_host(host)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::lock_guard lock(g_registered_host_mutex);
    if (g_registered_host.has_value()) return DIGITOR_RESULT_RESOURCE_IN_USE;
    g_registered_host = *host;
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_flutter_production_unregister_host(
    void* expected_user_data) {
    std::lock_guard host_lock(g_registered_host_mutex);
    if (!g_registered_host.has_value()) return DIGITOR_RESULT_NOT_INITIALIZED;
    if (g_registered_host->user_data != expected_user_data)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    {
        std::lock_guard sessions_lock(g_flutter_sessions_mutex);
        if (!g_flutter_sessions.empty()) return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    g_registered_host.reset();
    return DIGITOR_RESULT_OK;
}

int32_t digitor_flutter_production_host_registered(void) {
    std::lock_guard lock(g_registered_host_mutex);
    return g_registered_host.has_value() ? 1 : 0;
}

DigitorResult digitor_flutter_production_create_registered(
    const char* utf8_media_path,
    DigitorFlutterProductionSession** out_session) {
    DigitorFlutterProductionHost host{};
    {
        std::lock_guard lock(g_registered_host_mutex);
        if (!g_registered_host.has_value()) {
            if (out_session) *out_session = nullptr;
            return DIGITOR_RESULT_NOT_INITIALIZED;
        }
        host = *g_registered_host;
    }
    return digitor_flutter_production_create(&host, utf8_media_path, out_session);
}

DigitorResult digitor_flutter_production_create(
    const DigitorFlutterProductionHost* host,
    const char* utf8_media_path,
    DigitorFlutterProductionSession** out_session) {
    if (out_session) *out_session = nullptr;
    if (!valid_host(host) || !utf8_media_path || utf8_media_path[0] == '\0' ||
        !out_session) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    try {
        auto* session = new (std::nothrow) DigitorFlutterProductionSession{};
        if (!session) return DIGITOR_RESULT_OUT_OF_MEMORY;
        session->host = *host;

        char diagnostic[kDiagnosticCapacity]{};
        const auto result = session->host.open_media(
            session->host.user_data, utf8_media_path, diagnostic,
            kDiagnosticCapacity);
        if (result != DIGITOR_RESULT_OK) {
            session->last_error = diagnostic[0]
                ? diagnostic : "production media host failed to open media";
            delete session;
            return result;
        }
        session->media_open = true;

        {
            std::lock_guard registry_lock(g_flutter_sessions_mutex);
            if (!g_flutter_sessions.insert(session).second) {
                session->host.close_media(session->host.user_data);
                delete session;
                return DIGITOR_RESULT_INTERNAL_ERROR;
            }
        }
        *out_session = session;
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_flutter_production_destroy(
    DigitorFlutterProductionSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        DigitorFlutterProductionHost host{};
        DigitorNativeGpuTextureDescriptor preview{};
        bool has_preview = false;
        bool media_open = false;
        {
            std::unique_lock registry_lock(g_flutter_sessions_mutex);
            if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
            std::lock_guard session_lock(session->mutex);
            if (session->active_operation) return DIGITOR_RESULT_RESOURCE_IN_USE;
            host = session->host;
            preview = session->current_preview;
            has_preview = session->has_current_preview;
            media_open = session->media_open;
            session->current_preview = {};
            session->has_current_preview = false;
            session->media_open = false;
            session->graph = nullptr;
            g_flutter_sessions.erase(session);
        }
        if (has_preview) release_texture(host, preview);
        if (media_open) host.close_media(host.user_data);
        delete session;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_flutter_production_bind_node_graph(
    DigitorFlutterProductionSession* session,
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision) {
    if (!session || !graph || graph_revision == 0 || parameter_revision == 0)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto graph_result = validate_graph(graph);
    if (graph_result != DIGITOR_RESULT_OK) return graph_result;

    try {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        if (session->active_operation || session->has_current_preview)
            return DIGITOR_RESULT_RESOURCE_IN_USE;
        if (session->graph &&
            (graph_revision < session->graph_revision ||
             parameter_revision < session->parameter_revision)) {
            session->last_error = "production graph revisions must not move backwards";
            return DIGITOR_RESULT_INVALID_ARGUMENT;
        }
        session->graph = graph;
        session->graph_revision = graph_revision;
        session->parameter_revision = parameter_revision;
        session->last_error.clear();
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_flutter_production_preview(
    DigitorFlutterProductionSession* session,
    int64_t timestamp_us,
    uint32_t width,
    uint32_t height,
    DigitorNativeGpuTextureDescriptor* out_texture) {
    if (!session || !out_texture || width == 0 || height == 0)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_texture = {};

    DigitorFlutterProductionHost host{};
    DigitorNodeGraph* graph = nullptr;
    uint64_t graph_revision = 0;
    uint64_t parameter_revision = 0;
    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        if (!session->media_open || !session->graph)
            return DIGITOR_RESULT_NOT_INITIALIZED;
        if (session->active_operation || session->has_current_preview)
            return DIGITOR_RESULT_RESOURCE_IN_USE;
        session->active_operation = true;
        host = session->host;
        graph = session->graph;
        graph_revision = session->graph_revision;
        parameter_revision = session->parameter_revision;
    }

    DigitorNativeGpuTextureDescriptor texture{};
    texture.struct_size = sizeof(texture);
    texture.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
    char diagnostic[kDiagnosticCapacity]{};
    const auto result = host.render_frame(
        host.user_data, DIGITOR_FLUTTER_RENDER_PREVIEW, graph,
        graph_revision, parameter_revision, timestamp_us, width, height,
        &texture, diagnostic, kDiagnosticCapacity);

    bool release_failed_texture = false;
    DigitorResult final_result = result;
    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) {
            release_failed_texture = texture.native_handle != 0;
            final_result = DIGITOR_RESULT_RESOURCE_IN_USE;
        } else {
            std::lock_guard session_lock(session->mutex);
            session->active_operation = false;
            if (result != DIGITOR_RESULT_OK) {
                session->last_error = diagnostic[0]
                    ? diagnostic : "production preview host failed";
                release_failed_texture = texture.native_handle != 0;
            } else if (!valid_texture(texture, host, width, height, timestamp_us)) {
                session->last_error =
                    "production preview returned an incompatible, stale, or CPU texture";
                release_failed_texture = texture.native_handle != 0;
                final_result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            } else {
                session->current_preview = texture;
                session->has_current_preview = true;
                session->last_error.clear();
                *out_texture = texture;
                final_result = DIGITOR_RESULT_OK;
            }
        }
    }
    if (release_failed_texture) release_texture(host, texture);
    return final_result;
}

DigitorResult digitor_flutter_production_preview_consumed(
    DigitorFlutterProductionSession* session,
    uint64_t generation) {
    if (!session || generation == 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
    DigitorFlutterProductionHost host{};
    DigitorNativeGpuTextureDescriptor texture{};
    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        if (!session->has_current_preview)
            return DIGITOR_RESULT_NOT_INITIALIZED;
        if (session->current_preview.generation != generation)
            return DIGITOR_RESULT_INVALID_ARGUMENT;
        host = session->host;
        texture = session->current_preview;
        session->current_preview = {};
        session->has_current_preview = false;
    }
    release_texture(host, texture);
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_flutter_production_query_preview(
    DigitorFlutterProductionSession* session,
    DigitorNativePreviewCapabilities* out_capabilities) {
    if (!session || !out_capabilities) return DIGITOR_RESULT_INVALID_ARGUMENT;
    DigitorFlutterProductionHost host{};
    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        host = session->host;
    }
    *out_capabilities = {};
    out_capabilities->struct_size = sizeof(*out_capabilities);
    out_capabilities->api_version = DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION;
    const auto result = host.query_preview(host.user_data, out_capabilities);
    if (result != DIGITOR_RESULT_OK) return result;
    if (out_capabilities->struct_size < sizeof(*out_capabilities) ||
        out_capabilities->api_version != DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION ||
        out_capabilities->cpu_fallback_only ||
        !out_capabilities->native_gpu_preview_available) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_flutter_production_export(
    DigitorFlutterProductionSession* session,
    const DigitorFlutterExportRequest* request,
    DigitorExportProgressCallback progress,
    void* progress_user_data) {
    if (!session || !request ||
        request->struct_size < sizeof(DigitorFlutterExportRequest) ||
        request->api_version != DIGITOR_FLUTTER_EXPORT_REQUEST_VERSION ||
        !request->utf8_output_path || request->utf8_output_path[0] == '\0' ||
        request->last_frame < request->first_frame ||
        request->first_frame < 0 || request->width == 0 || request->height == 0) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    DigitorFlutterProductionHost host{};
    DigitorNodeGraph* graph = nullptr;
    uint64_t graph_revision = 0;
    uint64_t parameter_revision = 0;
    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        if (!session->media_open || !session->graph)
            return DIGITOR_RESULT_NOT_INITIALIZED;
        if (session->active_operation || session->has_current_preview)
            return DIGITOR_RESULT_RESOURCE_IN_USE;
        session->active_operation = true;
        host = session->host;
        graph = session->graph;
        graph_revision = session->graph_revision;
        parameter_revision = session->parameter_revision;
    }

    char diagnostic[kDiagnosticCapacity]{};
    const auto result = host.export_media(
        host.user_data, graph, graph_revision, parameter_revision, request,
        progress, progress_user_data, diagnostic, kDiagnosticCapacity);

    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        session->active_operation = false;
        if (result == DIGITOR_RESULT_OK) {
            session->last_error.clear();
        } else {
            session->last_error = diagnostic[0]
                ? diagnostic : "production export host failed";
        }
    }
    return result;
}

DigitorResult digitor_flutter_production_cancel(
    DigitorFlutterProductionSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    DigitorFlutterProductionHost host{};
    {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        host = session->host;
    }
    return host.cancel(host.user_data);
}

DigitorResult digitor_flutter_production_get_last_error(
    DigitorFlutterProductionSession* session,
    char* buffer,
    uint32_t* inout_size) {
    if (!session || !inout_size) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard registry_lock(g_flutter_sessions_mutex);
        if (!registered(session)) return DIGITOR_RESULT_RESOURCE_IN_USE;
        std::lock_guard session_lock(session->mutex);
        return copy_error(session->last_error, buffer, inout_size);
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

} // extern "C"
