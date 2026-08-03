#include "digitor/beauty.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace digitor {
namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float luma(const Color& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
Color mix(Color a, Color b, float t) {
    t = clamp01(t);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a};
}
Color sample(const Color* p, int x, int y, int w, int h) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return p[static_cast<std::size_t>(y) * w + x];
}
float matte_sample(const std::vector<float>& p, int x, int y, int w, int h) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return p[static_cast<std::size_t>(y) * w + x];
}

float soft_range(float v, float lo, float hi, float feather) {
    const float enter = clamp01((v - (lo - feather)) / feather);
    const float exit = clamp01(((hi + feather) - v) / feather);
    return enter * exit;
}

Color bilateral(const Color* input, int x, int y, int w, int h,
                float detail_protection) {
    const Color center = sample(input, x, y, w, h);
    const float center_y = luma(center);
    const float sigma = 0.025f + (1.0f - clamp01(detail_protection)) * 0.12f;
    Color sum{};
    sum.a = 0.0f;
    float weights = 0.0f;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const Color q = sample(input, x + dx, y + dy, w, h);
            const float spatial = std::exp(-0.32f * float(dx * dx + dy * dy));
            const float range = std::exp(-std::abs(luma(q) - center_y) / sigma);
            const float weight = spatial * range;
            sum.r += q.r * weight;
            sum.g += q.g * weight;
            sum.b += q.b * weight;
            weights += weight;
        }
    }
    if (weights <= 0.0f) return center;
    return {sum.r / weights, sum.g / weights, sum.b / weights, center.a};
}

void weighted_skin_mean(const Color* input, const std::vector<float>& matte,
                        std::size_t count, float& y, float& cb, float& cr) {
    double sy = 0.0, scb = 0.0, scr = 0.0, sw = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const float w = matte[i];
        if (w <= 0.001f) continue;
        const Color& c = input[i];
        const float py = luma(c);
        sy += py * w;
        scb += (c.b - py) * w;
        scr += (c.r - py) * w;
        sw += w;
    }
    if (sw <= 1e-6) { y = 0.5f; cb = 0.0f; cr = 0.0f; return; }
    y = static_cast<float>(sy / sw);
    cb = static_cast<float>(scb / sw);
    cr = static_cast<float>(scr / sw);
}

void process_image(const BeautyFrameContext& context, const BeautySettings& settings,
                   const Color* input, Color* output, std::size_t count,
                   std::vector<float>& matte) {
    const int w = static_cast<int>(context.width);
    const int h = static_cast<int>(context.height);
    const float amount = clamp01(settings.amount);
    const float edge = clamp01(settings.edge_protection);
    float mean_y{}, mean_cb{}, mean_cr{};
    if (settings.kind == BeautyKind::even_skin_tone)
        weighted_skin_mean(input, matte, count, mean_y, mean_cb, mean_cr);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const Color base = input[i];
            const float local_edge = std::abs(luma(sample(input, x - 1, y, w, h)) -
                                              luma(sample(input, x + 1, y, w, h))) +
                                     std::abs(luma(sample(input, x, y - 1, w, h)) -
                                              luma(sample(input, x, y + 1, w, h)));
            const float edge_guard = 1.0f - clamp01(local_edge * (2.0f + edge * 10.0f));
            const float mask = clamp01(matte[i] * (0.25f + 0.75f * edge_guard));
            Color result = base;

            if (settings.kind == BeautyKind::skin_brighten) {
                const float py = luma(base);
                const float highlight = 1.0f - clamp01((py - 0.65f) / 0.35f) *
                                              clamp01(settings.highlight_protection);
                const float lift = amount * mask * highlight * (context.hdr ? 0.10f : 0.16f);
                const float target_y = py + (1.0f - py) * lift;
                const float scale = py > 1e-5f ? target_y / py : 1.0f + lift;
                result = {base.r * scale, base.g * scale, base.b * scale, base.a};
            } else if (settings.kind == BeautyKind::skin_smooth) {
                const Color smooth = bilateral(input, x, y, w, h,
                                               settings.detail_protection);
                result = mix(base, smooth, amount * mask);
            } else if (settings.kind == BeautyKind::even_skin_tone) {
                const float py = luma(base);
                const float cb = base.b - py;
                const float cr = base.r - py;
                const float balanced_y = py + (mean_y - py) * 0.22f * amount;
                const float balanced_cb = cb + (mean_cb - cb) * 0.38f * amount;
                const float balanced_cr = cr + (mean_cr - cr) * 0.38f * amount;
                Color target{balanced_y + balanced_cr,
                             balanced_y - 0.1873f * balanced_cb - 0.4681f * balanced_cr,
                             balanced_y + balanced_cb, base.a};
                result = mix(base, target, mask);
            } else if (settings.kind == BeautyKind::blemish_reduction) {
                const Color smooth = bilateral(input, x, y, w, h, 0.35f);
                const float deficit = luma(smooth) - luma(base);
                const float spot = clamp01((deficit - 0.018f) / 0.12f);
                const float chroma_delta = std::abs((base.r - base.g) -
                                                    (smooth.r - smooth.g));
                const float chroma_guard = 1.0f - clamp01(chroma_delta * 5.0f);
                result = mix(base, smooth, amount * mask * spot * chroma_guard);
            }

            result.r = std::max(0.0f, result.r);
            result.g = std::max(0.0f, result.g);
            result.b = std::max(0.0f, result.b);
            result.a = base.a;
            output[i] = result;
        }
    }
}

} // namespace

std::vector<float> build_skin_matte(const Color* input, std::uint32_t width,
                                    std::uint32_t height,
                                    const float* external_matte,
                                    std::size_t external_count) {
    if (!input || !width || !height) return {};
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> raw(count);
    if (external_matte && external_count == count) {
        for (std::size_t i = 0; i < count; ++i) raw[i] = clamp01(external_matte[i]);
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            const Color& c = input[i];
            const float y = luma(c);
            const float cb = (c.b - y) * 0.564f + 0.5f;
            const float cr = (c.r - y) * 0.713f + 0.5f;
            const float chroma = soft_range(cb, 0.28f, 0.58f, 0.08f) *
                                 soft_range(cr, 0.43f, 0.75f, 0.08f);
            const float luminance = soft_range(y, 0.04f, 0.98f, 0.08f);
            const float channel_order = clamp01((c.r - c.b + 0.12f) * 4.0f) *
                                        clamp01((c.g - c.b + 0.10f) * 5.0f);
            raw[i] = clamp01(chroma * luminance * (0.35f + 0.65f * channel_order));
        }
    }

    std::vector<float> refined(count);
    const int w = static_cast<int>(width), h = static_cast<int>(height);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const Color center = sample(input, x, y, w, h);
        float sum = 0.0f, weights = 0.0f;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const Color q = sample(input, x + dx, y + dy, w, h);
            const float color_distance = std::abs(q.r - center.r) +
                                         std::abs(q.g - center.g) +
                                         std::abs(q.b - center.b);
            const float weight = std::exp(-color_distance * 8.0f);
            sum += matte_sample(raw, x + dx, y + dy, w, h) * weight;
            weights += weight;
        }
        refined[static_cast<std::size_t>(y) * w + x] =
            weights > 0.0f ? clamp01(sum / weights) : raw[static_cast<std::size_t>(y) * w + x];
    }
    return refined;
}

bool BeautyProcessor::process_cpu(const BeautyFrameContext& context,
                                  const BeautySettings& settings,
                                  const Color* input, Color* output,
                                  std::size_t count) {
    if (!input || !output || !context.width || !context.height ||
        count != static_cast<std::size_t>(context.width) * context.height ||
        !std::isfinite(settings.amount) || settings.amount < 0.0f || settings.amount > 1.0f)
        return false;

    std::lock_guard<std::mutex> lock(mutex_);
    auto matte = build_skin_matte(input, context.width, context.height,
                                  context.skin_matte, context.skin_matte_count);
    const bool sequential = previous_width_ == context.width &&
                            previous_height_ == context.height &&
                            previous_frame_ + 1 == context.frame &&
                            previous_matte_.size() == matte.size() && !context.scene_cut;
    if (sequential) {
        const float stability = clamp01(settings.temporal_stability);
        for (std::size_t i = 0; i < matte.size(); ++i)
            matte[i] = previous_matte_[i] * stability + matte[i] * (1.0f - stability);
    }
    process_image(context, settings, input, output, count, matte);
    previous_matte_ = std::move(matte);
    previous_width_ = context.width;
    previous_height_ = context.height;
    previous_frame_ = context.frame;
    return true;
}

bool BeautyProcessor::process_gpu(CommandEncoder& encoder,
                                  const BeautyFrameContext& context,
                                  const BeautySettings& settings,
                                  const Color* input, Color* output,
                                  std::size_t count) {
    if (!input || !output) return false;
    encoder.dispatch([this, context, settings, input, output, count] {
        if (!process_cpu(context, settings, input, output, count))
            throw std::runtime_error("beauty processing failed");
    });
    return true;
}

void BeautyProcessor::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    previous_matte_.clear();
    previous_width_ = previous_height_ = 0;
    previous_frame_ = -1;
}

} // namespace digitor
