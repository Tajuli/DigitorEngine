#include "digitor/gpu_image_session_c_api.h"

#include <cassert>
#include <cstring>

namespace {

struct HostState {
    int opens{};
    int processes{};
    int exports{};
    int releases{};
};

DigitorNativeGpuTextureDescriptor make_texture(uint64_t handle,
                                               uint32_t width,
                                               uint32_t height,
                                               int64_t timestamp) {
    DigitorNativeGpuTextureDescriptor value{};
    value.struct_size = sizeof(value);
    value.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
    value.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_VULKAN;
    value.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_VK_IMAGE;
    value.native_handle = handle;
    value.width = width;
    value.height = height;
    value.pixel_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
    value.timestamp_us = timestamp;
    value.generation = handle;
    value.device_identity = 41;
    value.context_identity = 42;
    value.readiness = DIGITOR_NATIVE_TEXTURE_READY;
    return value;
}

DigitorResult open_image(void* user_data, const char*,
                         DigitorNativeGpuTextureDescriptor* out,
                         char*, uint32_t) {
    auto& state = *static_cast<HostState*>(user_data);
    ++state.opens;
    *out = make_texture(100, 1920, 1080, 0);
    return DIGITOR_RESULT_OK;
}

DigitorResult process_image(void* user_data, DigitorGpuImageRenderMode,
                            const DigitorNativeGpuTextureDescriptor*,
                            uint32_t width, uint32_t height,
                            int64_t timestamp_us, uint64_t, uint64_t,
                            DigitorNativeGpuTextureDescriptor* out,
                            char*, uint32_t) {
    auto& state = *static_cast<HostState*>(user_data);
    ++state.processes;
    *out = make_texture(static_cast<uint64_t>(200 + state.processes),
                        width, height, timestamp_us);
    return DIGITOR_RESULT_OK;
}

DigitorResult export_image(void* user_data,
                           const DigitorNativeGpuTextureDescriptor* processed,
                           const char*, const DigitorImageExportOptions*,
                           char*, uint32_t) {
    auto& state = *static_cast<HostState*>(user_data);
    ++state.exports;
    return processed && processed->native_handle != 0
               ? DIGITOR_RESULT_OK
               : DIGITOR_RESULT_INVALID_ARGUMENT;
}

void release_texture(void* user_data,
                     const DigitorNativeGpuTextureDescriptor*) {
    ++static_cast<HostState*>(user_data)->releases;
}

}  // namespace

int main() {
    HostState state;
    DigitorGpuImageSessionHost host{};
    host.struct_size = sizeof(host);
    host.api_version = DIGITOR_GPU_IMAGE_SESSION_HOST_VERSION;
    host.user_data = &state;
    host.required_device_identity = 41;
    host.required_context_identity = 42;
    host.open_image = open_image;
    host.process_image = process_image;
    host.export_image = export_image;
    host.release_texture = release_texture;

    DigitorGpuImageSession* session = nullptr;
    assert(digitor_gpu_image_session_create(&host, "photo.png", &session) ==
           DIGITOR_RESULT_OK);
    assert(session != nullptr);
    assert(state.opens == 1);

    DigitorNativeGpuTextureDescriptor preview{};
    assert(digitor_gpu_image_session_render(
               session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1280, 720, 12,
               &preview) == DIGITOR_RESULT_OK);
    assert(preview.width == 1280 && preview.height == 720);
    assert(state.processes == 1);

    DigitorNativeGpuTextureDescriptor cached{};
    assert(digitor_gpu_image_session_render(
               session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1280, 720, 12,
               &cached) == DIGITOR_RESULT_OK);
    assert(cached.native_handle == preview.native_handle);
    assert(state.processes == 1);

    assert(digitor_gpu_image_session_set_parameter_revision(session, 1) ==
           DIGITOR_RESULT_OK);
    assert(digitor_gpu_image_session_render(
               session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1280, 720, 12,
               &cached) == DIGITOR_RESULT_OK);
    assert(state.processes == 2);

    DigitorImageExportOptions options{};
    options.struct_size = sizeof(options);
    options.api_version = DIGITOR_IMAGE_EXPORT_OPTIONS_VERSION;
    options.format = DIGITOR_IMAGE_EXPORT_PNG;
    options.quality = 100;
    options.preserve_alpha = 1;
    assert(digitor_gpu_image_session_export(session, "out.png", &options) ==
           DIGITOR_RESULT_OK);
    assert(state.exports == 1);

    assert(digitor_gpu_image_session_destroy(session) == DIGITOR_RESULT_OK);
    assert(digitor_gpu_image_session_destroy(session) ==
           DIGITOR_RESULT_RESOURCE_IN_USE);
    assert(digitor_gpu_image_session_render(
               session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1, 1, 0,
               &cached) == DIGITOR_RESULT_RESOURCE_IN_USE);

    uint32_t size = 0;
    assert(digitor_gpu_image_session_get_last_error(session, nullptr, &size) ==
           DIGITOR_RESULT_OK);
    assert(size > 1);
    char diagnostic[128]{};
    uint32_t capacity = sizeof(diagnostic);
    assert(digitor_gpu_image_session_get_last_error(
               session, diagnostic, &capacity) == DIGITOR_RESULT_OK);
    assert(std::strstr(diagnostic, "retired") != nullptr);
    assert(state.releases >= 3);
    return 0;
}
