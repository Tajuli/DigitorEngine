#pragma once

#include "digitor/color.hpp"
#include "digitor/commands.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace digitor {

enum class BeautyKind {
    skin_brighten,
    skin_smooth,
    even_skin_tone,
    blemish_reduction
};

struct BeautySettings {
    BeautyKind kind{BeautyKind::skin_brighten};
    float amount{0.5f};
    float detail_protection{0.7f};
    float edge_protection{0.8f};
    float temporal_stability{0.75f};
    float highlight_protection{0.8f};
};

struct BeautyFrameContext {
    std::uint32_t width{};
    std::uint32_t height{};
    std::int64_t frame{};
    std::uint64_t stream_id{};
    const float* skin_matte{};
    std::size_t skin_matte_count{};
    bool scene_cut{};
    bool hdr{};
};

class BeautyProcessor {
public:
    bool process_cpu(const BeautyFrameContext&, const BeautySettings&,
                     const Color* input, Color* output, std::size_t count);
    bool process_gpu(CommandEncoder&, const BeautyFrameContext&, const BeautySettings&,
                     const Color* input, Color* output, std::size_t count);
    void reset() noexcept;

private:
    std::vector<float> previous_matte_;
    std::uint32_t previous_width_{};
    std::uint32_t previous_height_{};
    std::int64_t previous_frame_{-1};
    std::mutex mutex_;
};

std::vector<float> build_skin_matte(const Color* input, std::uint32_t width,
                                    std::uint32_t height,
                                    const float* external_matte = nullptr,
                                    std::size_t external_count = 0);

} // namespace digitor
