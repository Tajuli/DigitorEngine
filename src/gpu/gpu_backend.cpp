#include "gpu/gpu_backend.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define DIGITOR_WINDOWS_VULKAN 1
#endif
#elif defined(__ANDROID__)
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <vulkan/vulkan.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace digitor {
namespace {

void copy_text(char* destination, std::size_t size, const char* source) {
    if (source != nullptr && size != 0) {
        std::strncpy(destination, source, size - 1);
        destination[size - 1] = '\0';
    }
}

class DeviceBackend final : public IRenderBackend {
public:
    explicit DeviceBackend(DigitorRendererInfo info) : info_(info) {}
    bool initialize(bool) override { return true; }
    void shutdown() noexcept override {}
    DigitorRendererInfo info() const noexcept override { return info_; }
private:
    DigitorRendererInfo info_{};
};

[[maybe_unused]] DigitorRendererInfo make_info(DigitorRendererBackend backend, const char* backend_name,
                              const char* device_name, bool compute, bool fp16) {
    DigitorRendererInfo info{};
    info.backend = backend;
    copy_text(info.backend_name, sizeof(info.backend_name), backend_name);
    copy_text(info.device_name, sizeof(info.device_name), device_name);
    info.is_gpu = 1;
    info.supports_compute = compute;
    info.supports_fp16 = fp16;
    info.supports_fp32 = 1;
    return info;
}

std::optional<DigitorRendererInfo> discover(DigitorRendererBackend backend) {
#if defined(_WIN32)
    if (backend == DIGITOR_RENDERER_VULKAN) {
#if defined(DIGITOR_WINDOWS_VULKAN)
        HMODULE library = LoadLibraryA("vulkan-1.dll");
        if (!library) return std::nullopt;
        auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr"));
        auto create = get_proc ? reinterpret_cast<PFN_vkCreateInstance>(get_proc(nullptr, "vkCreateInstance")) : nullptr;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "DigitorEngine", 1,
                              "DigitorEngine", VK_MAKE_VERSION(0,2,0), VK_API_VERSION_1_0};
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 0, nullptr};
        VkInstance instance{};
        if (!create || create(&ci, nullptr, &instance) != VK_SUCCESS) { FreeLibrary(library); return std::nullopt; }
        auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_proc(instance, "vkEnumeratePhysicalDevices"));
        auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get_proc(instance, "vkGetPhysicalDeviceProperties"));
        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get_proc(instance, "vkDestroyInstance"));
        uint32_t count = 0; std::optional<DigitorRendererInfo> result;
        if (enumerate && properties && enumerate(instance, &count, nullptr) == VK_SUCCESS && count) {
            std::vector<VkPhysicalDevice> devices(count);
            if (enumerate(instance, &count, devices.data()) == VK_SUCCESS) {
                VkPhysicalDeviceProperties p{};
                properties(devices[0], &p);
                result = make_info(backend, "Vulkan", p.deviceName, true, false);
            }
        }
        if (destroy) destroy(instance, nullptr); FreeLibrary(library); return result;
#else
        // Vulkan remains optional at build time; the D3D12 policy fallback still works without an SDK.
        return std::nullopt;
#endif
    }
    if (backend == DIGITOR_RENDERER_D3D12) {
        IDXGIFactory6* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return std::nullopt;
        std::optional<DigitorRendererInfo> result;
        for (UINT index = 0; ; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 description{};
            adapter->GetDesc1(&description);
            ID3D12Device* device = nullptr;
            if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
                char name[128] = "Direct3D 12 Adapter";
                WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name,
                                    static_cast<int>(sizeof(name)), nullptr, nullptr);
                result = make_info(backend, "Direct3D 12", name, true, true);
                device->Release(); adapter->Release(); break;
            }
            adapter->Release();
        }
        factory->Release();
        return result;
    }
#elif defined(__ANDROID__)
    if (backend == DIGITOR_RENDERER_VULKAN) {
        void* library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) return std::nullopt;
        auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
        if (!get_proc) { dlclose(library); return std::nullopt; }
        auto create = reinterpret_cast<PFN_vkCreateInstance>(get_proc(nullptr, "vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "DigitorEngine", 1,
                              "DigitorEngine", VK_MAKE_VERSION(0,2,0), VK_API_VERSION_1_0};
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 0, nullptr};
        VkInstance instance{};
        if (!create || create(&ci, nullptr, &instance) != VK_SUCCESS) { dlclose(library); return std::nullopt; }
        auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_proc(instance, "vkEnumeratePhysicalDevices"));
        auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get_proc(instance, "vkGetPhysicalDeviceProperties"));
        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get_proc(instance, "vkDestroyInstance"));
        uint32_t count = 0; std::optional<DigitorRendererInfo> result;
        if (enumerate && properties && enumerate(instance, &count, nullptr) == VK_SUCCESS && count) {
            std::vector<VkPhysicalDevice> devices(count);
            if (enumerate(instance, &count, devices.data()) == VK_SUCCESS) {
                VkPhysicalDeviceProperties p{};
                properties(devices[0], &p);
                result = make_info(backend, "Vulkan", p.deviceName, true, false);
            }
        }
        if (destroy) destroy(instance, nullptr); dlclose(library); return result;
    }
    if (backend == DIGITOR_RENDERER_OPENGL_ES) {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) return std::nullopt;
        const char* vendor = eglQueryString(display, EGL_VENDOR);
        auto result = make_info(backend, "OpenGL ES", vendor ? vendor : "Android GLES device",
                                false, false);
        eglTerminate(display); return result;
    }
#elif defined(__APPLE__)
    if (backend == DIGITOR_RENDERER_METAL) {
        using CreateDevice = id (*)();
        auto create = reinterpret_cast<CreateDevice>(dlsym(RTLD_DEFAULT, "MTLCreateSystemDefaultDevice"));
        id device = create ? create() : nullptr;
        if (!device) return std::nullopt;
        using SendId = id (*)(id, SEL);
        using SendCString = const char* (*)(id, SEL);
        id name = reinterpret_cast<SendId>(objc_msgSend)(device, sel_registerName("name"));
        const char* utf8 = name ? reinterpret_cast<SendCString>(objc_msgSend)(name, sel_registerName("UTF8String")) : nullptr;
        return make_info(backend, "Metal", utf8 ? utf8 : "Apple GPU", true, true);
    }
#endif
    (void)backend;
    return std::nullopt;
}

std::vector<DigitorRendererBackend> platform_order(HostPlatform platform) {
    switch (platform) {
        case HostPlatform::Windows: return {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_D3D12};
        case HostPlatform::Android: return {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_OPENGL_ES};
        case HostPlatform::IOS:
        case HostPlatform::MacOS: return {DIGITOR_RENDERER_METAL};
        default: return {};
    }
}
} // namespace

std::unique_ptr<IRenderBackend> select_gpu_backend(HostPlatform platform,
        DigitorRendererBackend preferred, const BackendFactory& factory) {
    auto order = platform_order(platform);
    if (preferred != DIGITOR_RENDERER_AUTO && preferred != DIGITOR_RENDERER_CPU) {
        // An explicit backend is a request for that backend, not a hint to try
        // unrelated GPU APIs before the Engine applies its CPU fallback policy.
        if (std::find(order.begin(), order.end(), preferred) == order.end()) {
            return nullptr;
        }
        order = {preferred};
    } else if (preferred == DIGITOR_RENDERER_CPU) {
        return nullptr;
    }
    for (auto candidate : order) if (auto backend = factory(candidate)) return backend;
    return nullptr;
}

std::unique_ptr<IRenderBackend> create_gpu_backend(DigitorRendererBackend preferred) {
    return select_gpu_backend(current_platform(), preferred, [](DigitorRendererBackend backend) {
        auto info = discover(backend);
        return info ? std::make_unique<DeviceBackend>(*info) : nullptr;
    });
}
} // namespace digitor
