#pragma once

#include "digitor/hardware_decode_qualification.hpp"
#include "digitor/media.hpp"
#include "digitor/native_media.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace digitor {

struct ProductionHardwareDecodeOptions {
    DigitorRendererBackend renderer_backend{DIGITOR_RENDERER_AUTO};
    DigitorPixelFormat render_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
    bool require_zero_copy{true};
    bool require_monotonic_timestamps{true};
};

struct ProductionDecodedFrame {
    FrameNumber number{};
    std::int64_t pts{};
    std::int64_t duration{};
    ProcessedGpuFramePtr gpu_frame;
    NativeMediaSurfacePtr decoder_surface;
};

using ProductionNativeImport =
    std::function<DigitorResult(const ZeroCopyImportRequest&, ProcessedGpuFramePtr&)>;

class ProductionHardwareDecodeSession final {
public:
    ProductionHardwareDecodeSession(std::unique_ptr<VideoDecoder> decoder,
                                    ProductionNativeImport importer,
                                    ProductionHardwareDecodeOptions options = {});

    ProductionHardwareDecodeSession(const ProductionHardwareDecodeSession&) = delete;
    ProductionHardwareDecodeSession& operator=(const ProductionHardwareDecodeSession&) = delete;

    [[nodiscard]] DigitorResult decode(FrameNumber frame_number,
                                       ProductionDecodedFrame& output,
                                       std::string* diagnostic = nullptr) noexcept;
    [[nodiscard]] DigitorResult seek(std::int64_t pts_us,
                                     std::string* diagnostic = nullptr) noexcept;
    [[nodiscard]] HardwareDecodeQualification qualification() const;
    [[nodiscard]] DecoderInfo decoder_info() const;

private:
    void fail_qualification(const std::string& diagnostic) noexcept;

    std::unique_ptr<VideoDecoder> decoder_;
    ProductionNativeImport importer_;
    ProductionHardwareDecodeOptions options_;
    mutable std::mutex mutex_;
    HardwareDecodeQualification qualification_{};
    bool have_timestamp_{};
    std::int64_t last_timestamp_{};
};

} // namespace digitor
