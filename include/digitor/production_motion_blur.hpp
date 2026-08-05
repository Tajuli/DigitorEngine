#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
namespace digitor {
struct MotionBlurPixel { float r{}, g{}, b{}, a{1.0f}; };
struct MotionVector { float x{}, y{}, confidence{1.0f}; };
struct MotionBlurFrame { std::uint32_t width{}, height{}; std::vector<MotionBlurPixel> pixels; };
struct MotionBlurSettings { float shutter_angle{180.0f}; std::uint32_t samples{8u}; float motion_scale{1.0f}; float confidence_floor{0.15f}; bool center_exposure{true}; };
enum class MotionBlurStatus : std::uint32_t { invalid=0u, ready=1u, backend_unavailable=2u, dispatch_failed=3u };
enum class MotionBlurBackend : std::uint32_t { cpu=0u, vulkan=1u, d3d12=2u, metal=3u, gles=4u };
struct MotionBlurResult { MotionBlurStatus status{MotionBlurStatus::invalid}; std::uint64_t digest{}; };
struct MotionBlurDispatchPacket { MotionBlurBackend backend{MotionBlurBackend::cpu}; std::uint32_t width{}, height{}; std::uint64_t input_handle{}, motion_handle{}, output_handle{}, command_handle{}; MotionBlurSettings settings{}; };
using MotionBlurDispatch = std::function<bool(const MotionBlurDispatchPacket&)>;
MotionBlurResult apply_motion_blur_reference(const MotionBlurFrame&, const std::vector<MotionVector>&, MotionBlurFrame&, const MotionBlurSettings&);
MotionBlurResult dispatch_motion_blur_gpu(const MotionBlurDispatchPacket&, const MotionBlurDispatch&);
std::uint64_t motion_blur_frame_digest(const MotionBlurFrame&) noexcept;
}
