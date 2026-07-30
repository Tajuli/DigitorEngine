#include "digitor/native_node_hardware_qualification.hpp"
#include "digitor/native_node_shader_contracts.hpp"
#include "digitor/native_node_pipeline_runtime.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace digitor {
namespace {
std::string escape_json(const std::string& value) {
  std::ostringstream out;
  for (const unsigned char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        else out << static_cast<char>(c);
    }
  }
  return out.str();
}
}

const char* native_node_backend_name(DigitorRendererBackend backend) noexcept {
  switch (backend) {
    case DIGITOR_RENDERER_VULKAN: return "vulkan";
    case DIGITOR_RENDERER_D3D12: return "d3d12";
    case DIGITOR_RENDERER_METAL: return "metal";
    case DIGITOR_RENDERER_OPENGL_ES: return "gles";
    case DIGITOR_RENDERER_CPU: return "cpu";
    default: return "unknown";
  }
}
const char* native_node_kernel_name(NativeNodeKernel kernel) noexcept {
  switch (kernel) {
    case NativeNodeKernel::parallel_mixer: return "parallel_mixer";
    case NativeNodeKernel::masked_composite: return "masked_composite";
    default: return "unknown";
  }
}

bool validate_native_node_hardware_evidence(
    const NativeNodeHardwareQualificationEvidence& e, double tolerance,
    std::string& diagnostic) noexcept {
  diagnostic.clear();
  if (e.backend == DIGITOR_RENDERER_CPU) { diagnostic = "hardware evidence cannot use CPU backend"; return false; }
  const auto expected = native_node_pipeline_contract_hash(e.backend, e.kernel);
  if (e.contract_hash == 0 || e.contract_hash != expected) { diagnostic = "pipeline contract hash mismatch"; return false; }
  if (e.device_identity == 0) { diagnostic = "missing exact device identity"; return false; }
  if (e.platform.empty() || e.device_name.empty() || e.driver_version.empty()) { diagnostic = "missing platform/device/driver identity"; return false; }
  if (e.evidence_id.empty()) { diagnostic = "missing evidence id"; return false; }
  if (!e.pipeline_created) { diagnostic = "native pipeline creation did not pass"; return false; }
  if (!e.dispatch_recorded) { diagnostic = "native dispatch did not pass"; return false; }
  if (!e.gpu_completed) { diagnostic = "GPU completion did not pass"; return false; }
  if (!e.cpu_gpu_parity) { diagnostic = "CPU/GPU parity did not pass"; return false; }
  if (!e.no_cpu_readback) { diagnostic = "intermediate CPU readback was observed"; return false; }
  if (!e.pipeline_cache_reused) { diagnostic = "pipeline cache reuse did not pass"; return false; }
  if (!e.retirement_invalidated) { diagnostic = "device retirement invalidation did not pass"; return false; }
  if (!std::isfinite(e.max_abs_error) || e.max_abs_error < 0.0 || e.max_abs_error > tolerance) {
    diagnostic = "numerical error exceeds qualification tolerance"; return false;
  }
  return true;
}

std::string native_node_hardware_evidence_json(const NativeNodeHardwareQualificationEvidence& e) {
  std::ostringstream out;
  out << std::boolalpha << std::setprecision(17)
      << "{\"schema\":1,\"backend\":\"" << native_node_backend_name(e.backend)
      << "\",\"kernel\":\"" << native_node_kernel_name(e.kernel)
      << "\",\"contract_hash\":" << e.contract_hash
      << ",\"device_identity\":" << e.device_identity
      << ",\"platform\":\"" << escape_json(e.platform)
      << "\",\"device_name\":\"" << escape_json(e.device_name)
      << "\",\"driver_version\":\"" << escape_json(e.driver_version)
      << "\",\"pipeline_created\":" << e.pipeline_created
      << ",\"dispatch_recorded\":" << e.dispatch_recorded
      << ",\"gpu_completed\":" << e.gpu_completed
      << ",\"cpu_gpu_parity\":" << e.cpu_gpu_parity
      << ",\"no_cpu_readback\":" << e.no_cpu_readback
      << ",\"pipeline_cache_reused\":" << e.pipeline_cache_reused
      << ",\"retirement_invalidated\":" << e.retirement_invalidated
      << ",\"max_abs_error\":" << e.max_abs_error
      << ",\"evidence_id\":\"" << escape_json(e.evidence_id) << "\"}";
  return out.str();
}
} // namespace digitor
