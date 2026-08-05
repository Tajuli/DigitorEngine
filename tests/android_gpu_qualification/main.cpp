#include "digitor/android_gpu_qualification.hpp"
int main() {
  using namespace digitor;
  AndroidGpuQualificationInput input{};
  input.backend = AndroidGpuBackend::vulkan;
  input.manufacturer = "Vendor";
  input.model = "PhysicalDevice";
  input.hardware = "soc-hardware";
  input.renderer = "MobileGPU";
  input.driver_version = "driver-1";
  input.sdk_level = 35u;
  input.is_physical_device = 1u;
  input.native_submission_completed = 1u;
  input.gpu_timestamp_valid = 1u;
  input.preview_digest = 42u;
  input.export_digest = 42u;
  if (qualify_android_gpu(input).status != AndroidGpuQualificationStatus::qualified) return 1;
  input.renderer = "SwiftShader";
  if (qualify_android_gpu(input).status != AndroidGpuQualificationStatus::blocked_software_renderer) return 2;
  input.renderer = "MobileGPU";
  input.fallback_dispatches = 1u;
  if (qualify_android_gpu(input).status != AndroidGpuQualificationStatus::blocked_fallback) return 3;
  input.fallback_dispatches = 0u;
  input.export_digest = 43u;
  if (qualify_android_gpu(input).status != AndroidGpuQualificationStatus::blocked_parity) return 4;
  return 0;
}
