#include "digitor/gpu_image_session_c_api.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>

struct DigitorGpuImageSession {
    std::mutex mutex;
    DigitorGpuImageSessionHost host{};
    DigitorNativeGpuTextureDescriptor source{};
    DigitorNativeGpuTextureDescriptor cached{};
    DigitorGpuImageRenderMode cached_mode{DIGITOR_GPU_IMAGE_RENDER_PREVIEW};
    uint32_t cached_width{};
    uint32_t cached_height{};
    int64_t cached_timestamp{};
    uint64_t graph_revision{};
    uint64_t parameter_revision{};
    uint64_t cached_graph_revision{};
    uint64_t cached_parameter_revision{};
    std::string last_error;
    bool has_source{};
    bool has_cached{};
    bool active{true};
};

namespace {

constexpr uint32_t kDiagnosticCapacity = 512;

bool valid_gpu_descriptor(const DigitorNativeGpuTextureDescriptor& value,
                          const DigitorGpuImageSessionHost& host) noexcept {
    if (value.struct_size < sizeof(DigitorNativeGpuTextureDescriptor) ||
        value.api_version != DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION ||
        value.readiness != DIGITOR_NATIVE_TEXTURE_READY ||
        value.width == 0 || value.height == 0 || value.native_handle == 0 ||
        value.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_NONE ||
        value.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_CPU_RGBA8 ||
        value.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
        value.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
        return false;
    }
    if (host.required_device_identity != 0 &&
        value.device_identity != host.required_device_identity) {
        return false;
    }
    return host.required_context_identity == 0 ||
           value.context_identity == host.required_context_identity;
}

void release_texture(DigitorGpuImageSession& session,
                     DigitorNativeGpuTextureDescriptor& texture) noexcept {
    if (texture.native_handle != 0 && session.host.release_texture) {
        session.host.release_texture(session.host.user_data, &texture);
    }
    texture = {};
}

void invalidate_cache(DigitorGpuImageSession& session) noexcept {
    if (session.has_cached) release_texture(session, session.cached);
    session.has_cached = false;
}

DigitorResult fail(DigitorGpuImageSession* session, DigitorResult result,
                   std::string message) noexcept {
    if (session) session->last_error = std::move(message);
    return result;
}

DigitorResult validate_session(DigitorGpuImageSession* session) noexcept {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (!session->active) {
        session->last_error = "GPU image session handle is retired";
        return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    return DIGITOR_RESULT_OK;
}

DigitorResult render_locked(DigitorGpuImageSession& session,
                            DigitorGpuImageRenderMode mode,
                            uint32_t width, uint32_t height,
                            int64_t timestamp_us,
                            DigitorNativeGpuTextureDescriptor* out_texture) {
    if (!out_texture) return fail(&session, DIGITOR_RESULT_INVALID_ARGUMENT,
                                  "out_texture is null");
    *out_texture = {};
    if (mode != DIGITOR_GPU_IMAGE_RENDER_PREVIEW &&
        mode != DIGITOR_GPU_IMAGE_RENDER_EXPORT) {
        return fail(&session, DIGITOR_RESULT_INVALID_ARGUMENT,
                    "invalid GPU image render mode");
    }
    if (width == 0) width = session.source.width;
    if (height == 0) height = session.source.height;

    if (session.has_cached && session.cached_mode == mode &&
        session.cached_width == width && session.cached_height == height &&
        session.cached_timestamp == timestamp_us &&
        session.cached_graph_revision == session.graph_revision &&
        session.cached_parameter_revision == session.parameter_revision) {
        *out_texture = session.cached;
        session.last_error.clear();
        return DIGITOR_RESULT_OK;
    }

    char diagnostic[kDiagnosticCapacity]{};
    DigitorNativeGpuTextureDescriptor output{};
    output.struct_size = sizeof(output);
    output.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
    const auto result = session.host.process_image(
        session.host.user_data, mode, &session.source, width, height,
        timestamp_us, session.graph_revision, session.parameter_revision,
        &output, diagnostic, kDiagnosticCapacity);
    if (result != DIGITOR_RESULT_OK) {
        return fail(&session, result,
                    diagnostic[0] ? diagnostic : "GPU image processing failed");
    }
    if (!valid_gpu_descriptor(output, session.host) ||
        output.width != width || output.height != height ||
        output.timestamp_us != timestamp_us) {
        if (session.host.release_texture && output.native_handle != 0) {
            session.host.release_texture(session.host.user_data, &output);
        }
        return fail(&session, DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                    "GPU image processing returned an incompatible or CPU frame");
    }

    invalidate_cache(session);
    session.cached = output;
    session.cached_mode = mode;
    session.cached_width = width;
    session.cached_height = height;
    session.cached_timestamp = timestamp_us;
    session.cached_graph_revision = session.graph_revision;
    session.cached_parameter_revision = session.parameter_revision;
    session.has_cached = true;
    *out_texture = output;
    session.last_error.clear();
    return DIGITOR_RESULT_OK;
}

}  // namespace

extern "C" {

DigitorResult digitor_gpu_image_session_create(
    const DigitorGpuImageSessionHost* host, const char* utf8_path,
    DigitorGpuImageSession** out_session) {
    if (out_session) *out_session = nullptr;
    if (!host || !utf8_path || utf8_path[0] == '\0' || !out_session ||
        host->struct_size < sizeof(DigitorGpuImageSessionHost) ||
        host->api_version != DIGITOR_GPU_IMAGE_SESSION_HOST_VERSION ||
        !host->open_image || !host->process_image || !host->export_image ||
        !host->release_texture) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    try {
        auto session = std::make_unique<DigitorGpuImageSession>();
        session->host = *host;
        session->source.struct_size = sizeof(session->source);
        session->source.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
        char diagnostic[kDiagnosticCapacity]{};
        const auto result = host->open_image(host->user_data, utf8_path,
                                             &session->source, diagnostic,
                                             kDiagnosticCapacity);
        if (result != DIGITOR_RESULT_OK) return result;
        if (!valid_gpu_descriptor(session->source, *host)) {
            if (session->source.native_handle != 0) {
                host->release_texture(host->user_data, &session->source);
            }
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }
        session->has_source = true;
        *out_session = session.release();
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_gpu_image_session_destroy(DigitorGpuImageSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(session->mutex);
        if (!session->active) return DIGITOR_RESULT_RESOURCE_IN_USE;
        invalidate_cache(*session);
        if (session->has_source) release_texture(*session, session->source);
        session->has_source = false;
        session->active = false;
        session->last_error = "GPU image session handle is retired";
        /* Keep the small tombstone allocation alive so stale and double-destroy
         * handles are rejected deterministically instead of dereferencing freed
         * memory across the C/FFI boundary. */
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_gpu_image_session_set_graph_revision(
    DigitorGpuImageSession* session, uint64_t revision) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(session->mutex);
        const auto valid = validate_session(session);
        if (valid != DIGITOR_RESULT_OK) return valid;
        session->graph_revision = revision;
        invalidate_cache(*session);
        session->last_error.clear();
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_gpu_image_session_set_parameter_revision(
    DigitorGpuImageSession* session, uint64_t revision) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(session->mutex);
        const auto valid = validate_session(session);
        if (valid != DIGITOR_RESULT_OK) return valid;
        session->parameter_revision = revision;
        invalidate_cache(*session);
        session->last_error.clear();
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_gpu_image_session_render(
    DigitorGpuImageSession* session, DigitorGpuImageRenderMode mode,
    uint32_t width, uint32_t height, int64_t timestamp_us,
    DigitorNativeGpuTextureDescriptor* out_texture) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(session->mutex);
        const auto valid = validate_session(session);
        if (valid != DIGITOR_RESULT_OK) return valid;
        return render_locked(*session, mode, width, height, timestamp_us,
                             out_texture);
    } catch (const std::bad_alloc&) {
        return fail(session, DIGITOR_RESULT_OUT_OF_MEMORY, "out of memory");
    } catch (...) {
        return fail(session, DIGITOR_RESULT_INTERNAL_ERROR,
                    "unexpected exception at GPU image C boundary");
    }
}

DigitorResult digitor_gpu_image_session_export(
    DigitorGpuImageSession* session, const char* utf8_output_path,
    const DigitorImageExportOptions* options) {
    if (!session || !utf8_output_path || utf8_output_path[0] == '\0' ||
        !options || options->struct_size < sizeof(DigitorImageExportOptions) ||
        options->api_version != DIGITOR_IMAGE_EXPORT_OPTIONS_VERSION ||
        options->quality > 100) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    try {
        std::lock_guard lock(session->mutex);
        const auto valid = validate_session(session);
        if (valid != DIGITOR_RESULT_OK) return valid;
        DigitorNativeGpuTextureDescriptor output{};
        auto result = render_locked(*session, DIGITOR_GPU_IMAGE_RENDER_EXPORT,
                                    options->width, options->height, 0, &output);
        if (result != DIGITOR_RESULT_OK) return result;
        char diagnostic[kDiagnosticCapacity]{};
        result = session->host.export_image(
            session->host.user_data, &output, utf8_output_path, options,
            diagnostic, kDiagnosticCapacity);
        if (result != DIGITOR_RESULT_OK) {
            return fail(session, result,
                        diagnostic[0] ? diagnostic : "GPU image export failed");
        }
        session->last_error.clear();
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return fail(session, DIGITOR_RESULT_OUT_OF_MEMORY, "out of memory");
    } catch (...) {
        return fail(session, DIGITOR_RESULT_INTERNAL_ERROR,
                    "unexpected exception at GPU image export boundary");
    }
}

DigitorResult digitor_gpu_image_session_get_last_error(
    DigitorGpuImageSession* session, char* buffer, uint32_t* inout_size) {
    if (!session || !inout_size) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(session->mutex);
        const auto required = static_cast<uint32_t>(session->last_error.size() + 1);
        if (!buffer) {
            *inout_size = required;
            return DIGITOR_RESULT_OK;
        }
        if (*inout_size < required) {
            *inout_size = required;
            return DIGITOR_RESULT_INVALID_ARGUMENT;
        }
        std::memcpy(buffer, session->last_error.c_str(), required);
        *inout_size = required;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

}  // extern "C"
