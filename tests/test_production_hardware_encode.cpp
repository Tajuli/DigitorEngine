#include "digitor/production_hardware_encode.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <string>

using namespace digitor;

namespace {
ProcessedGpuFramePtr make_frame(std::int64_t pts, std::uint64_t identity = 1) {
  static int context;
  GpuFrameMetadata metadata{};
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  metadata.timestamp = pts;
  return std::make_shared<ProcessedGpuFrame>(
      &context, DIGITOR_RENDERER_D3D12, metadata, identity,
      std::static_pointer_cast<void>(std::make_shared<int>(1)),
      std::make_shared<std::atomic_bool>(true), false);
}

HardwareEncodeConfig config() {
  HardwareEncodeConfig value{};
  value.backend = EncoderBackend::nvenc;
  value.output_path = "output.mp4";
  value.duration_us = 100000;
  value.profile.width = 1920;
  value.profile.height = 1080;
  value.profile.fps_num = 30;
  value.profile.fps_den = 1;
  value.profile.codec = ExportCodec::h264;
  value.profile.ten_bit = true;
  return value;
}
}  // namespace

int main() {
  std::uint64_t opened = 0;
  std::uint64_t submitted = 0;
  std::uint64_t drained = 0;
  std::uint64_t finalized = 0;
  std::uint64_t cancelled = 0;

  HardwareEncoderCallbacks callbacks{};
  callbacks.open = [&](const HardwareEncodeConfig& value, std::string&) {
    assert(value.backend == EncoderBackend::nvenc);
    ++opened;
    return DIGITOR_RESULT_OK;
  };
  callbacks.submit_gpu_frame = [&](const HardwareEncodeFrame& value, std::string&) {
    assert(value.frame && value.frame->backend() == DIGITOR_RENDERER_D3D12);
    ++submitted;
    return DIGITOR_RESULT_OK;
  };
  callbacks.drain = [&](std::string&) { ++drained; return DIGITOR_RESULT_OK; };
  callbacks.finalize_atomic = [&](std::string&) { ++finalized; return DIGITOR_RESULT_OK; };
  callbacks.cancel = [&] { ++cancelled; };

  ProductionHardwareEncodeSession session(config(), callbacks);
  std::string diagnostic;
  assert(session.start(&diagnostic) == DIGITOR_RESULT_OK);
  assert(diagnostic.empty());
  assert(session.submit({make_frame(0), 0, 33333, true}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.submit({make_frame(33333, 2), 33333, 33333, false}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.submit({make_frame(66666, 3), 66666, 33334, false}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.finish(&diagnostic) == DIGITOR_RESULT_OK);

  const auto telemetry = session.telemetry();
  assert(telemetry.state == HardwareEncodeState::completed);
  assert(telemetry.submitted_frames == 3);
  assert(telemetry.accepted_frames == 3);
  assert(telemetry.rejected_frames == 0);
  assert(telemetry.cpu_readbacks == 0);
  assert(telemetry.progress == 1.0);
  assert(opened == 1 && submitted == 3 && drained == 1 && finalized == 1);
  assert(cancelled == 0);

  ProductionHardwareEncodeSession timestamps(config(), callbacks);
  assert(timestamps.start() == DIGITOR_RESULT_OK);
  assert(timestamps.submit({make_frame(100, 4), 100, 33, false}) == DIGITOR_RESULT_OK);
  assert(timestamps.submit({make_frame(100, 5), 100, 33, false}) != DIGITOR_RESULT_OK);
  assert(timestamps.telemetry().state == HardwareEncodeState::failed);

  auto software = config();
  software.backend = EncoderBackend::software;
  ProductionHardwareEncodeSession strict_software(software, callbacks);
  assert(strict_software.start() != DIGITOR_RESULT_OK);

  ProductionHardwareEncodeSession cancelled_session(config(), callbacks);
  assert(cancelled_session.start() == DIGITOR_RESULT_OK);
  cancelled_session.cancel();
  assert(cancelled_session.telemetry().state == HardwareEncodeState::cancelled);
  assert(cancelled == 1);
  return 0;
}
