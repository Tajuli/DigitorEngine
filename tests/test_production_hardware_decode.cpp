#include "digitor/production_hardware_decode.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace digitor;

class FakeDecoder final : public VideoDecoder {
public:
    explicit FakeDecoder(bool cpu_pixels = false) : cpu_pixels_(cpu_pixels) {}

    std::shared_ptr<VideoFrame> decode(FrameNumber number) override {
        auto frame = std::make_shared<VideoFrame>();
        frame->number = number;
        frame->pts = number * 33333;
        frame->duration = 33333;
        frame->width = 1920;
        frame->height = 1080;
        if(cpu_pixels_) frame->pixels.push_back({0.f, 0.f, 0.f, 1.f});

        NativeMediaSurfaceDescriptor descriptor{};
        descriptor.struct_size = sizeof(descriptor);
        descriptor.api_version = 1;
        descriptor.platform = NativeMediaPlatform::windows;
        descriptor.handle_type = NativeMediaHandleType::d3d11_texture2d;
        descriptor.pixel_format = NativeMediaPixelFormat::nv12;
        descriptor.width = frame->width;
        descriptor.height = frame->height;
        descriptor.plane_count = 2;
        descriptor.native_handle = 0x1000u + static_cast<std::uintptr_t>(number);
        descriptor.timestamp_us = frame->pts;
        frame->native_surface = std::make_shared<NativeMediaSurface>(
            descriptor, std::static_pointer_cast<void>(std::make_shared<int>(1)));
        return frame;
    }

    void seek(std::int64_t) override {}
    DecoderInfo info() const override {
        return {HardwareDecode::dxva, true, "fake D3D11VA", true,
                NativeMediaHandleType::d3d11_texture2d};
    }
private:
    bool cpu_pixels_{};
};

ProcessedGpuFramePtr make_gpu_frame(std::int64_t timestamp) {
    GpuFrameMetadata metadata{};
    metadata.width = 1920;
    metadata.height = 1080;
    metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
    metadata.timestamp = timestamp;
    return std::make_shared<ProcessedGpuFrame>(
        reinterpret_cast<void*>(0x1), DIGITOR_RENDERER_D3D12, metadata, 42,
        std::static_pointer_cast<void>(std::make_shared<int>(1)),
        std::make_shared<std::atomic_bool>(true), false);
}
}

int main() {
    using namespace digitor;

    ProductionHardwareDecodeOptions options{};
    options.renderer_backend = DIGITOR_RENDERER_D3D12;
    ProductionHardwareDecodeSession session(
        std::make_unique<FakeDecoder>(),
        [](const ZeroCopyImportRequest& request, ProcessedGpuFramePtr& output) {
            output = make_gpu_frame(request.surface->descriptor().timestamp_us);
            return DIGITOR_RESULT_OK;
        }, options);

    ProductionDecodedFrame frame{};
    std::string diagnostic;
    assert(session.decode(0, frame, &diagnostic) == DIGITOR_RESULT_OK);
    assert(frame.gpu_frame);
    assert(frame.decoder_surface);
    const auto q = session.qualification();
    assert(q.status == HardwareDecodeQualificationStatus::passed);
    assert(q.hardware_frame_received);
    assert(q.native_surface_exported);
    assert(q.render_backend_imported);
    assert(q.cpu_readbacks == 0);

    ProductionHardwareDecodeSession rejected(
        std::make_unique<FakeDecoder>(true),
        [](const ZeroCopyImportRequest&, ProcessedGpuFramePtr&) {
            return DIGITOR_RESULT_OK;
        }, options);
    assert(rejected.decode(0, frame, &diagnostic) == DIGITOR_RESULT_INTERNAL_ERROR);
    assert(rejected.qualification().cpu_readback_observed);

    ProductionHardwareDecodeSession import_failure(
        std::make_unique<FakeDecoder>(),
        [](const ZeroCopyImportRequest&, ProcessedGpuFramePtr&) {
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }, options);
    assert(import_failure.decode(0, frame, &diagnostic) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

    assert(session.seek(1000000, &diagnostic) == DIGITOR_RESULT_OK);
    return 0;
}
