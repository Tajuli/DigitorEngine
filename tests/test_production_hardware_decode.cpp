#include "digitor/hardware_media_end_to_end.hpp"
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
        if (cached_frame_ && cached_frame_->number == number) return cached_frame_;

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
        cached_frame_ = frame;
        return frame;
    }

    void seek(std::int64_t) override {}
    DecoderInfo info() const override {
        return {HardwareDecode::dxva, true, "fake D3D11VA", true,
                NativeMediaHandleType::d3d11_texture2d};
    }
private:
    bool cpu_pixels_{};
    std::shared_ptr<VideoFrame> cached_frame_;
};

class RandomAccessFakeDecoder final : public VideoDecoder {
public:
    std::shared_ptr<VideoFrame> decode(FrameNumber number) override {
        return frame(number, number * 10'000);
    }
    std::shared_ptr<VideoFrame> decode_at_timestamp(std::int64_t pts) override {
        ++timestamp_decodes;
        // Model VFR selection: the first source PTS at or after the request.
        const std::int64_t selected = ((pts + 9'999) / 10'000) * 10'000;
        if (cached_timestamp_frame_ && cached_timestamp_frame_->pts == selected)
            return cached_timestamp_frame_;
        cached_timestamp_frame_ = frame(0, selected);
        return cached_timestamp_frame_;
    }
    void seek(std::int64_t pts) override { seeks.push_back(pts); }
    DecoderInfo info() const override {
        return {HardwareDecode::dxva, true, "random-access fake D3D11VA", true,
                NativeMediaHandleType::d3d11_texture2d};
    }
    int timestamp_decodes{};
    std::vector<std::int64_t> seeks;
private:
    static std::shared_ptr<VideoFrame> frame(FrameNumber number,
                                             std::int64_t pts) {
        auto result = std::make_shared<VideoFrame>();
        result->number = number;
        result->pts = pts;
        result->duration = 10'000;
        result->width = 1920;
        result->height = 1080;
        NativeMediaSurfaceDescriptor descriptor{};
        descriptor.struct_size = sizeof(descriptor);
        descriptor.api_version = 1;
        descriptor.platform = NativeMediaPlatform::windows;
        descriptor.handle_type = NativeMediaHandleType::d3d11_texture2d;
        descriptor.pixel_format = NativeMediaPixelFormat::nv12;
        descriptor.width = result->width;
        descriptor.height = result->height;
        descriptor.native_handle = static_cast<std::uintptr_t>(pts + 1);
        descriptor.timestamp_us = pts;
        result->native_surface = std::make_shared<NativeMediaSurface>(
            descriptor, std::static_pointer_cast<void>(std::make_shared<int>(1)));
        return result;
    }

    std::shared_ptr<VideoFrame> cached_timestamp_frame_;
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
    const auto first_decoder_surface = frame.decoder_surface;

    // Production decode must not consume native_surface from a cached
    // VideoFrame. The same frame may be requested again by playback/preview.
    ProductionDecodedFrame repeated_frame{};
    assert(session.decode(0, repeated_frame, &diagnostic) == DIGITOR_RESULT_OK);
    assert(repeated_frame.gpu_frame);
    assert(repeated_frame.decoder_surface == first_decoder_surface);

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
        [](const ZeroCopyImportRequest& request, ProcessedGpuFramePtr&) {
            assert(request.diagnostic);
            *request.diagnostic =
                "D3D12 OpenSharedHandle failed: HRESULT=0x80070057";
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }, options);
    assert(import_failure.decode(0, frame, &diagnostic) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    assert(diagnostic ==
           "D3D12 OpenSharedHandle failed: HRESULT=0x80070057");
    assert(import_failure.qualification().diagnostic == diagnostic);

    assert(session.seek(1000000, &diagnostic) == DIGITOR_RESULT_OK);

    auto random_decoder = std::make_unique<RandomAccessFakeDecoder>();
    auto* random_observer = random_decoder.get();
    options.require_monotonic_timestamps = false;
    ProductionHardwareDecodeSession random_access(
        std::move(random_decoder),
        [](const ZeroCopyImportRequest& request, ProcessedGpuFramePtr& output) {
            output = make_gpu_frame(request.surface->descriptor().timestamp_us);
            return DIGITOR_RESULT_OK;
        }, options);
    for (const auto requested : {0LL, 250'000LL, 600'000LL, 50'000LL,
                                 590'001LL, 50'000LL, 0LL, 0LL}) {
        diagnostic = "stale";
        assert(random_access.decode_at_timestamp(requested, frame, &diagnostic) ==
               DIGITOR_RESULT_OK);
        assert(frame.pts >= requested);
        assert(frame.pts - requested < 10'000);
        assert(diagnostic.empty());
        assert(frame.gpu_frame && frame.decoder_surface);
    }
    assert(random_observer->timestamp_decodes == 8);
    const auto random_q = random_access.qualification();
    assert(random_q.hardware_frame_received);
    assert(random_q.native_surface_exported);
    assert(random_q.render_backend_imported);
    assert(random_q.cpu_readbacks == 0);

    ProductionHardwareDecodeSession e2e_decoder(
        std::make_unique<FakeDecoder>(),
        [](const ZeroCopyImportRequest& request, ProcessedGpuFramePtr& output) {
            output = make_gpu_frame(request.surface->descriptor().timestamp_us);
            return DIGITOR_RESULT_OK;
        }, options);

    HardwareEncodeConfig encode_config{};
    encode_config.backend = EncoderBackend::nvenc;
    encode_config.output_path = "qualified-output.mp4";
    encode_config.duration_us = 66666;
    encode_config.profile.width = 1920;
    encode_config.profile.height = 1080;
    encode_config.profile.fps_num = 30;
    encode_config.profile.fps_den = 1;
    encode_config.profile.codec = ExportCodec::h264;

    std::uint64_t submitted = 0;
    HardwareEncoderCallbacks callbacks{};
    callbacks.open = [](const HardwareEncodeConfig&, std::string&) {
        return DIGITOR_RESULT_OK;
    };
    callbacks.submit_gpu_frame = [&](const HardwareEncodeFrame& input, std::string&) {
        assert(input.frame && input.frame->backend() == DIGITOR_RENDERER_D3D12);
        ++submitted;
        return DIGITOR_RESULT_OK;
    };
    callbacks.drain = [](std::string&) { return DIGITOR_RESULT_OK; };
    callbacks.finalize_atomic = [](std::string&) { return DIGITOR_RESULT_OK; };
    callbacks.cancel = [] {};

    ProductionHardwareEncodeSession e2e_encoder(encode_config, callbacks);
    HardwareMediaEndToEndSession e2e(
        e2e_decoder, e2e_encoder,
        [](const ProductionDecodedFrame& decoded, ProcessedGpuFramePtr& output,
           std::string&) {
            output = make_gpu_frame(decoded.pts);
            return DIGITOR_RESULT_OK;
        });
    assert(e2e.start(&diagnostic) == DIGITOR_RESULT_OK);
    assert(e2e.process_frame(0, true, &diagnostic) == DIGITOR_RESULT_OK);
    assert(e2e.process_frame(1, false, &diagnostic) == DIGITOR_RESULT_OK);
    assert(e2e.finish(&diagnostic) == DIGITOR_RESULT_OK);
    assert(submitted == 2);
    const auto& e2e_q = e2e.qualification();
    assert(e2e_q.passed());
    assert(e2e_q.decoded_frames == 2);
    assert(e2e_q.processed_frames == 2);
    assert(e2e_q.encoded_frames == 2);
    assert(e2e_q.native_decoder_surface_retained);
    assert(e2e_q.decode_zero_copy);
    assert(e2e_q.renderer_backend_continuity);
    assert(e2e_q.timestamp_parity);
    assert(e2e_q.encode_zero_copy);

    return 0;
}