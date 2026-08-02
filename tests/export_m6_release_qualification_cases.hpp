#pragma once

#include "digitor/export_release_qualification.hpp"

#include <cassert>
#include <vector>

inline digitor::ExportPlatformQualification m6_qualified_platform(
    digitor::ExportQualificationPlatform platform,
    DigitorRendererBackend renderer,
    digitor::EncoderBackend encoder,
    const char* hardware) {
  using namespace digitor;
  ExportPlatformQualification value{};
  value.platform = platform;
  value.evidence_kind = ExportEvidenceKind::physical_device;
  value.renderer_backend = renderer;
  value.encoder_backend = encoder;
  value.codec = "h264";
  value.container = "mp4";
  value.parity = {true, true, true, 0.001, 0.0001, true, true, true};
  value.runtime.audio_video_sync_verified = true;
  value.runtime.max_av_sync_error_us = 10000;
  value.runtime.vfr_verified = true;
  value.runtime.cancellation_verified = true;
  value.runtime.resume_verified = true;
  value.runtime.device_loss_or_codec_reset_verified = true;
  value.runtime.long_run_verified = true;
  value.runtime.tested_duration_us = 30ULL * 60ULL * 1000000ULL;
  value.artifact.artifact_retained = true;
  value.artifact.hardware_identity_retained = true;
  value.artifact.codec_and_container_reported = true;
  value.artifact.unsupported_combinations_reported = true;
  value.artifact.artifact_identity = "sha256:test-artifact";
  value.artifact.hardware_identity = hardware;
  return value;
}

inline void run_export_m6_release_qualification_cases() {
  using namespace digitor;
  auto windows = m6_qualified_platform(ExportQualificationPlatform::windows, DIGITOR_RENDERER_D3D12, EncoderBackend::nvenc, "windows-gpu");
  auto android = m6_qualified_platform(ExportQualificationPlatform::android, DIGITOR_RENDERER_VULKAN, EncoderBackend::media_codec, "android-gpu");
  auto macos = m6_qualified_platform(ExportQualificationPlatform::macos, DIGITOR_RENDERER_METAL, EncoderBackend::video_toolbox, "mac-gpu");
  auto ios = m6_qualified_platform(ExportQualificationPlatform::ios, DIGITOR_RENDERER_METAL, EncoderBackend::video_toolbox, "iphone-gpu");
  assert(validate_export_platform_qualification(windows));
  assert(validate_export_release_matrix({windows, android, macos, ios}));
  auto mock_only = windows; mock_only.evidence_kind = ExportEvidenceKind::contract_test;
  assert(!validate_export_platform_qualification(mock_only));
  auto readback = windows; readback.runtime.cpu_readbacks = 1;
  assert(!validate_export_platform_qualification(readback));
  auto parity_failure = windows; parity_failure.parity.max_absolute_error = 0.01;
  assert(!validate_export_platform_qualification(parity_failure));
  assert(!validate_export_release_matrix({windows, android, macos}));
}
