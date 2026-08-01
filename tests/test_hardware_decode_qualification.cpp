#include "digitor/hardware_decode_qualification.hpp"

#include <cassert>
#include <cstring>

int main() {
    using namespace digitor;

    HardwareDecodeQualification q{};
    assert(!hardware_decode_qualification_complete(q));
    assert(std::strcmp(hardware_decode_qualification_failure(q),
                       "qualification run did not pass") == 0);

    q.status = HardwareDecodeQualificationStatus::passed;
    q.decoder = HardwareDecode::dxva;
    q.handle_type = NativeMediaHandleType::d3d11_texture2d;
    q.hardware_frame_received = true;
    q.native_surface_exported = true;
    q.render_backend_imported = true;
    q.timestamp_verified = true;
    q.release_verified = true;
    q.decoded_frames = 120;
    assert(hardware_decode_qualification_complete(q));
    assert(std::strcmp(hardware_decode_qualification_failure(q), "") == 0);

    q.cpu_readback_observed = true;
    q.cpu_readbacks = 1;
    assert(!hardware_decode_qualification_complete(q));
    assert(std::strcmp(hardware_decode_qualification_failure(q),
                       "CPU readback occurred in the zero-copy path") == 0);

    q.cpu_readback_observed = false;
    q.cpu_readbacks = 0;
    q.render_backend_imported = false;
    assert(!hardware_decode_qualification_complete(q));
    assert(std::strcmp(hardware_decode_qualification_failure(q),
                       "render backend did not import the native surface") == 0);

    q.render_backend_imported = true;
    q.release_verified = false;
    assert(!hardware_decode_qualification_complete(q));
    assert(std::strcmp(hardware_decode_qualification_failure(q),
                       "decoder surface release was not verified") == 0);

    return 0;
}
