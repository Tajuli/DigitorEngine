#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <vector>

#include "gpu/gpu_backend.hpp"

namespace digitor {
namespace {
using Microsoft::WRL::ComPtr;

void copy_text(char* destination, std::size_t capacity, std::string_view source) noexcept {
    if (capacity == 0) return;
    const auto count = std::min(capacity - 1, source.size());
    if (count != 0) std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

struct D3DObject { ComPtr<ID3D12Resource> resource; };

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_) CloseHandle(handle_);
        handle_ = handle;
    }
private:
    HANDLE handle_{};
};

class MappedResource {
public:
    MappedResource() = default;
    ~MappedResource() { reset(); }
    MappedResource(const MappedResource&) = delete;
    MappedResource& operator=(const MappedResource&) = delete;

    HRESULT map(ID3D12Resource* resource, const D3D12_RANGE* read_range) noexcept {
        reset();
        void* data = nullptr;
        const HRESULT hr = resource->Map(0, read_range, &data);
        if (SUCCEEDED(hr)) { resource_ = resource; data_ = data; }
        return hr;
    }
    void reset(const D3D12_RANGE* written_range = nullptr) noexcept {
        if (resource_) resource_->Unmap(0, written_range);
        resource_ = nullptr;
        data_ = nullptr;
    }
    [[nodiscard]] std::uint8_t* bytes() const noexcept {
        return static_cast<std::uint8_t*>(data_);
    }
private:
    ID3D12Resource* resource_{};
    void* data_{};
};

DXGI_FORMAT format(DigitorPixelFormat value) noexcept {
    switch (value) {
        case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DigitorResult result(HRESULT hr) noexcept {
    return hr == E_OUTOFMEMORY ? DIGITOR_RESULT_OUT_OF_MEMORY
                               : (FAILED(hr) ? DIGITOR_RESULT_BACKEND_UNAVAILABLE : DIGITOR_RESULT_OK);
}

class D3DBackend final : public IRenderBackend {
public:
    explicit D3DBackend(ComPtr<ID3D12Device> device) : device_(std::move(device)) {
        info_.backend = DIGITOR_RENDERER_D3D12;
        copy_text(info_.backend_name, sizeof(info_.backend_name), "Direct3D 12");
        copy_text(info_.device_name, sizeof(info_.device_name), "D3D12 Adapter");
        info_.is_gpu = info_.supports_compute = info_.supports_fp16 = info_.supports_fp32 = 1;
    }
    ~D3DBackend() override { shutdown(); }

    bool initialize(bool) override {
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)))) return false;
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&allocator_)))) return false;
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
                                               nullptr, IID_PPV_ARGS(&list_)))) return false;
        if (FAILED(list_->Close())) return false;
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
        fence_event_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
        if (!fence_event_.get()) return false;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                &blob, &error))) return false;
        if (FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                IID_PPV_ARGS(&root_signature_)))) return false;

        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = 1;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap_)))) return false;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        return SUCCEEDED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap_)));
    }

    void shutdown() noexcept override {
        if (queue_ && fence_) (void)signal_and_wait();
        rtv_heap_.Reset(); srv_heap_.Reset(); root_signature_.Reset(); fence_.Reset();
        list_.Reset(); allocator_.Reset(); queue_.Reset(); fence_event_.reset();
    }
    [[nodiscard]] DigitorRendererInfo info() const noexcept override { return info_; }

    DigitorResult render_rgba8(uint32_t width, uint32_t height, std::span<const uint8_t> source,
                               std::vector<uint8_t>& destination) noexcept override {
        const std::size_t pixel_bytes = std::size_t(width) * height * 4;
        if (!width || !height || (!source.empty() && source.size() != pixel_bytes))
            return DIGITOR_RESULT_INVALID_ARGUMENT;

        D3D12_RESOURCE_DESC texture_desc{};
        texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_desc.Width = width; texture_desc.Height = height;
        texture_desc.DepthOrArraySize = 1; texture_desc.MipLevels = 1;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> target;
        HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&target));
        if (FAILED(hr)) return result(hr);

        UINT64 total_bytes = 0;
        UINT64 row_bytes = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        device_->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr, &row_bytes,
                                       &total_bytes);
        D3D12_RESOURCE_DESC buffer_desc{};
        buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer_desc.Width = total_bytes; buffer_desc.Height = 1;
        buffer_desc.DepthOrArraySize = 1; buffer_desc.MipLevels = 1;
        buffer_desc.SampleDesc.Count = 1;
        buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
        if (FAILED(hr)) return result(hr);
        ComPtr<ID3D12Resource> readback;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
        if (FAILED(hr)) return result(hr);

        MappedResource upload_map;
        D3D12_RANGE no_read{0, 0};
        hr = upload_map.map(upload.Get(), &no_read);
        if (FAILED(hr)) return result(hr);
        for (UINT y = 0; y < height; ++y) {
            auto* row = upload_map.bytes() + y * row_bytes;
            if (!source.empty()) std::memcpy(row, source.data() + std::size_t(y) * width * 4,
                                              std::size_t(width) * 4);
            else for (UINT x = 0; x < width; ++x) {
                row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 0;
                row[x * 4 + 3] = 255;
            }
        }
        upload_map.reset();

        hr = allocator_->Reset();
        if (FAILED(hr)) return result(hr);
        hr = list_->Reset(allocator_.Get(), nullptr);
        if (FAILED(hr)) return result(hr);
        D3D12_TEXTURE_COPY_LOCATION upload_location{};
        upload_location.pResource = upload.Get();
        upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        upload_location.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION target_location{};
        target_location.pResource = target.Get();
        target_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list_->CopyTextureRegion(&target_location, 0, 0, 0, &upload_location, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = target.Get();
        barrier.Transition.Subresource = 0;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list_->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION readback_location{};
        readback_location.pResource = readback.Get();
        readback_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        readback_location.PlacedFootprint = footprint;
        list_->CopyTextureRegion(&readback_location, 0, 0, 0, &target_location, nullptr);
        hr = list_->Close();
        if (FAILED(hr)) return result(hr);
        ID3D12CommandList* command_lists[]{list_.Get()};
        queue_->ExecuteCommandLists(1, command_lists);
        const auto wait_result = signal_and_wait();
        if (wait_result != DIGITOR_RESULT_OK) return wait_result;

        try { destination.resize(pixel_bytes); }
        catch (const std::bad_alloc&) { return DIGITOR_RESULT_OUT_OF_MEMORY; }
        catch (...) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
        MappedResource readback_map;
        D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_bytes)};
        hr = readback_map.map(readback.Get(), &read_range);
        if (FAILED(hr)) return result(hr);
        for (UINT y = 0; y < height; ++y)
            std::memcpy(destination.data() + std::size_t(y) * width * 4,
                        readback_map.bytes() + y * row_bytes, std::size_t(width) * 4);
        D3D12_RANGE no_write{0, 0};
        readback_map.reset(&no_write);
        return DIGITOR_RESULT_OK;
    }

    DigitorResult create_texture(const DigitorTextureDesc& desc, void** out) noexcept override {
        if (!out) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        auto object = std::unique_ptr<D3DObject>(new (std::nothrow) D3DObject);
        if (!object) return DIGITOR_RESULT_OUT_OF_MEMORY;
        D3D12_RESOURCE_DESC resource{};
        resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource.Width = desc.width; resource.Height = desc.height;
        resource.DepthOrArraySize = 1; resource.MipLevels = 1;
        resource.Format = format(desc.format); resource.SampleDesc.Count = 1;
        resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (desc.usage & DIGITOR_TEXTURE_USAGE_RENDER_TARGET)
            resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (desc.usage & DIGITOR_TEXTURE_USAGE_STORAGE)
            resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&object->resource));
        if (FAILED(hr)) return result(hr);
        *out = object.release();
        return DIGITOR_RESULT_OK;
    }

    DigitorResult create_buffer(const DigitorBufferDesc& desc, void** out) noexcept override {
        if (!out) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        auto object = std::unique_ptr<D3DObject>(new (std::nothrow) D3DObject);
        if (!object) return DIGITOR_RESULT_OUT_OF_MEMORY;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = (desc.usage & (DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING))
                        ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC resource{};
        resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource.Width = desc.size; resource.Height = 1;
        resource.DepthOrArraySize = 1; resource.MipLevels = 1;
        resource.SampleDesc.Count = 1; resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (desc.usage & DIGITOR_BUFFER_USAGE_STORAGE)
            resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto state = heap.Type == D3D12_HEAP_TYPE_UPLOAD
                               ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;
        const HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
            state, nullptr, IID_PPV_ARGS(&object->resource));
        if (FAILED(hr)) return result(hr);
        *out = object.release();
        return DIGITOR_RESULT_OK;
    }

    DigitorResult create_sampler(const DigitorSamplerDesc& desc, void** out) noexcept override {
        if (!out) return DIGITOR_RESULT_INVALID_ARGUMENT;
        auto* sampler = new (std::nothrow) DigitorSamplerDesc(desc);
        *out = sampler;
        return sampler ? DIGITOR_RESULT_OK : DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    DigitorResult map_buffer(void* pointer, uint64_t offset, uint64_t, void** out) noexcept override {
        if (!pointer || !out) return DIGITOR_RESULT_INVALID_ARGUMENT;
        *out = nullptr;
        auto* object = static_cast<D3DObject*>(pointer);
        D3D12_RANGE no_read{0, 0};
        void* base = nullptr;
        const HRESULT hr = object->resource->Map(0, &no_read, &base);
        if (FAILED(hr)) return result(hr);
        *out = static_cast<unsigned char*>(base) + offset;
        return DIGITOR_RESULT_OK;
    }
    void unmap_buffer(void* pointer) noexcept override {
        if (pointer) static_cast<D3DObject*>(pointer)->resource->Unmap(0, nullptr);
    }
    void destroy_texture(void* pointer) noexcept override { delete static_cast<D3DObject*>(pointer); }
    void destroy_buffer(void* pointer) noexcept override { delete static_cast<D3DObject*>(pointer); }
    void destroy_sampler(void* pointer) noexcept override {
        delete static_cast<DigitorSamplerDesc*>(pointer);
    }

private:
    DigitorResult signal_and_wait() noexcept {
        const UINT64 value = ++fence_value_;
        HRESULT hr = queue_->Signal(fence_.Get(), value);
        if (FAILED(hr)) return result(hr);
        if (fence_->GetCompletedValue() >= value) return DIGITOR_RESULT_OK;
        hr = fence_->SetEventOnCompletion(value, fence_event_.get());
        if (FAILED(hr)) return result(hr);
        return WaitForSingleObject(fence_event_.get(), INFINITE) == WAIT_OBJECT_0
                   ? DIGITOR_RESULT_OK : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    UniqueHandle fence_event_;
    UINT64 fence_value_{};
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    DigitorRendererInfo info_{};
};
} // namespace

std::unique_ptr<IRenderBackend> create_native_backend(DigitorRendererBackend backend) {
#ifdef DIGITOR_HAS_VULKAN
    extern std::unique_ptr<IRenderBackend> create_vulkan_backend();
    if (backend == DIGITOR_RENDERER_VULKAN) return create_vulkan_backend();
#endif
    if (backend != DIGITOR_RENDERER_D3D12) return nullptr;
    if (gpu_validation_requested()) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return nullptr;
    return std::make_unique<D3DBackend>(std::move(device));
}
} // namespace digitor
#endif
