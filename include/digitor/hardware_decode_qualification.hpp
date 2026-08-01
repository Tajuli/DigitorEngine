#pragma once

#include "digitor/media.hpp"
#include "digitor/native_media.hpp"

#include <cstdint>
#include <string>

namespace digitor {

enum class HardwareDecodeQualificationStatus : std::uint32_t {
    not_run = 0,
    passed = 1,
    failed = 2,
    unsupported = 3
};

struct HardwareDecodeQualification {
    std::uint32_t struct_size{sizeof(HardwareDecodeQualification)};
    std::uint32_t api_version{1};
    HardwareDecode decoder{HardwareDecode::cpu};
    NativeMediaHandleType handle_type{NativeMediaHandleType::none};
    HardwareDecodeQualificationStatus status{HardwareDecodeQualificationStatus::not_run};
    bool hardware_frame_received{};
    bool native_surface_exported{};
    bool render_backend_imported{};
    bool cpu_readback_observed{};
    bool timestamp_verified{};
    bool release_verified{};
    std::uint64_t decoded_frames{};
    std::uint64_t dropped_frames{};
    std::uint64_t cpu_readbacks{};
    double average_decode_ms{};
    std::string diagnostic;
};

[[nodiscard]] inline bool hardware_decode_qualification_complete(
    const HardwareDecodeQualification& q) noexcept {
    return q.struct_size >= sizeof(HardwareDecodeQualification) &&
           q.api_version == 1 &&
           q.status == HardwareDecodeQualificationStatus::passed &&
           q.decoder != HardwareDecode::cpu &&
           q.handle_type != NativeMediaHandleType::none &&
           q.hardware_frame_received &&
           q.native_surface_exported &&
           q.render_backend_imported &&
           !q.cpu_readback_observed &&
           q.cpu_readbacks == 0 &&
           q.timestamp_verified &&
           q.release_verified &&
           q.decoded_frames > 0;
}

[[nodiscard]] inline const char* hardware_decode_qualification_failure(
    const HardwareDecodeQualification& q) noexcept {
    if(q.struct_size < sizeof(HardwareDecodeQualification)) return "qualification struct is too small";
    if(q.api_version != 1) return "unsupported qualification API version";
    if(q.status != HardwareDecodeQualificationStatus::passed) return "qualification run did not pass";
    if(q.decoder == HardwareDecode::cpu) return "software decoder cannot qualify hardware decode";
    if(!q.hardware_frame_received) return "no hardware frame was received";
    if(!q.native_surface_exported || q.handle_type == NativeMediaHandleType::none) return "native decoder surface was not exported";
    if(!q.render_backend_imported) return "render backend did not import the native surface";
    if(q.cpu_readback_observed || q.cpu_readbacks != 0) return "CPU readback occurred in the zero-copy path";
    if(!q.timestamp_verified) return "timestamp ordering was not verified";
    if(!q.release_verified) return "decoder surface release was not verified";
    if(q.decoded_frames == 0) return "no frames were decoded";
    return "";
}

} // namespace digitor
