#pragma once
#include "digitor/digitor.h"
#include "digitor/native_node_shader_contracts.hpp"
#include <cstdint>
#include <string>

namespace digitor {

struct NativeNodeHardwareQualificationEvidence {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  NativeNodeKernel kernel{NativeNodeKernel::parallel_mixer};
  std::uint64_t contract_hash{};
  std::uint64_t device_identity{};
  std::string platform;
  std::string device_name;
  std::string driver_version;
  bool pipeline_created{};
  bool dispatch_recorded{};
  bool gpu_completed{};
  bool cpu_gpu_parity{};
  bool no_cpu_readback{};
  bool pipeline_cache_reused{};
  bool retirement_invalidated{};
  double max_abs_error{};
  std::string evidence_id;
};

[[nodiscard]] bool validate_native_node_hardware_evidence(
    const NativeNodeHardwareQualificationEvidence&, double max_allowed_error,
    std::string& diagnostic) noexcept;

[[nodiscard]] std::string native_node_hardware_evidence_json(
    const NativeNodeHardwareQualificationEvidence&);

[[nodiscard]] const char* native_node_backend_name(DigitorRendererBackend) noexcept;
[[nodiscard]] const char* native_node_kernel_name(NativeNodeKernel) noexcept;

} // namespace digitor
