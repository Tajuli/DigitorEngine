#include "digitor/qualifier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace digitor {
namespace {
float linear_weight(float value, const QualifierRange& range) {
    const float softness = std::max(0.0f, range.softness);
    if (value >= range.low && value <= range.high) return 1.0f;
    if (softness > 0.0f && value < range.low && value > range.low - softness)
        return (value - range.low + softness) / softness;
    if (softness > 0.0f && value > range.high && value < range.high + softness)
        return (range.high + softness - value) / softness;
    return 0.0f;
}

float hue_weight(float hue, const QualifierRange& range) {
    // Hue is circular. A low value greater than high denotes a range crossing red.
    if (range.low <= range.high) return linear_weight(hue, range);
    QualifierRange upper{range.low, 1.0f, range.softness};
    QualifierRange lower{0.0f, range.high, range.softness};
    return std::max(linear_weight(hue, upper), linear_weight(hue, lower));
}

void rgb_to_hsl(Color color, float& hue, float& saturation, float& luminance) {
    const float high = std::max({color.r, color.g, color.b});
    const float low = std::min({color.r, color.g, color.b});
    const float delta = high - low;
    luminance = (high + low) * 0.5f;
    saturation = delta == 0.0f ? 0.0f : delta / std::max(1e-8f, 1.0f - std::abs(2.0f * luminance - 1.0f));
    hue = 0.0f;
    if (delta == 0.0f) return;
    if (high == color.r) hue = std::fmod((color.g - color.b) / delta, 6.0f);
    else if (high == color.g) hue = (color.b - color.r) / delta + 2.0f;
    else hue = (color.r - color.g) / delta + 4.0f;
    hue /= 6.0f;
    if (hue < 0.0f) hue += 1.0f;
}

void validate(const QualifierSettings& settings) {
    auto range = [](const QualifierRange& value, const char* name) {
        if (!std::isfinite(value.low) || !std::isfinite(value.high) || !std::isfinite(value.softness) ||
            value.low < 0.0f || value.low > 1.0f || value.high < 0.0f || value.high > 1.0f || value.softness < 0.0f)
            throw std::invalid_argument(std::string("invalid qualifier ") + name);
    };
    range(settings.hue, "hue"); range(settings.saturation, "saturation"); range(settings.luminance, "luminance");
    if (settings.blur < 0.0f || settings.denoise < 0.0f || settings.clean_black < 0.0f ||
        settings.clean_black > 1.0f || settings.clean_white < 0.0f || settings.clean_white > 1.0f)
        throw std::invalid_argument("invalid qualifier cleanup setting");
}
}

void HslQualifier::sample(Color color) {
    float hue, saturation, luminance; rgb_to_hsl(color, hue, saturation, luminance);
    settings_.hue = {hue, hue, 0.05f};
    settings_.saturation = {saturation, saturation, 0.1f};
    settings_.luminance = {luminance, luminance, 0.1f};
}

void HslQualifier::sample(std::span<const Color> colors) {
    if (colors.empty()) throw std::invalid_argument("eye dropper sample is empty");
    double sine{}, cosine{}, saturation{}, luminance{};
    constexpr double tau = 6.28318530717958647692;
    for (auto color : colors) {
        float h, s, l; rgb_to_hsl(color, h, s, l);
        sine += std::sin(h * tau); cosine += std::cos(h * tau); saturation += s; luminance += l;
    }
    float hue = static_cast<float>(std::atan2(sine, cosine) / tau); if (hue < 0.0f) hue += 1.0f;
    const float count = static_cast<float>(colors.size());
    settings_.hue = {hue, hue, 0.05f};
    settings_.saturation = {static_cast<float>(saturation) / count, static_cast<float>(saturation) / count, 0.1f};
    settings_.luminance = {static_cast<float>(luminance) / count, static_cast<float>(luminance) / count, 0.1f};
}

std::vector<float> HslQualifier::matte_cpu(std::span<const Color> input, uint32_t width, uint32_t height) const {
    if (input.size() != static_cast<std::size_t>(width) * height) throw std::invalid_argument("image size does not match dimensions");
    validate(settings_); std::vector<float> matte(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        float hue, saturation, luminance; rgb_to_hsl(input[index], hue, saturation, luminance);
        float value = hue_weight(hue, settings_.hue) * linear_weight(saturation, settings_.saturation) * linear_weight(luminance, settings_.luminance);
        if (value <= settings_.clean_black) value = 0.0f;
        if (value >= 1.0f - settings_.clean_white) value = 1.0f;
        matte[index] = settings_.invert ? 1.0f - value : value;
    }
    if (settings_.denoise > 0.0f && width && height) {
        auto source = matte; const float amount = std::clamp(settings_.denoise, 0.0f, 1.0f);
        for (uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
            std::array<float,9> window{};std::size_t count{};
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){auto yy=std::clamp<int>(static_cast<int>(y)+dy,0,height-1);auto xx=std::clamp<int>(static_cast<int>(x)+dx,0,width-1);window[count++]=source[yy*width+xx];}
            std::nth_element(window.begin(),window.begin()+4,window.end()); matte[y*width+x]+=amount*(window[4]-matte[y*width+x]);
        }
    }
    const auto radius=static_cast<uint32_t>(std::ceil(settings_.blur));
    if(radius&&width&&height){auto source=matte;for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){double sum{};std::size_t count{};for(int dy=-static_cast<int>(radius);dy<=static_cast<int>(radius);++dy)for(int dx=-static_cast<int>(radius);dx<=static_cast<int>(radius);++dx){auto yy=std::clamp<int>(static_cast<int>(y)+dy,0,height-1);auto xx=std::clamp<int>(static_cast<int>(x)+dx,0,width-1);sum+=source[yy*width+xx];++count;}matte[y*width+x]=static_cast<float>(sum/count);}}
    return matte;
}

void HslQualifier::matte_gpu(CommandEncoder& encoder, std::span<const Color> input, std::span<float> output, uint32_t width, uint32_t height) const {
    if (output.size() != input.size()) throw std::invalid_argument("matte output size mismatch");
    encoder.dispatch([this,input,output,width,height]{auto result=matte_cpu(input,width,height);std::copy(result.begin(),result.end(),output.begin());});
}
}
