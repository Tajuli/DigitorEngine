#pragma once
#include "digitor/production_node_graph.hpp"
#include "digitor/native_node_hardware_qualification.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class NativeNodeSignoffState : std::uint32_t {
  missing,
  implemented_unqualified,
  qualified
};

struct NativeNodeOperationEvidence {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  NodeOperationKind operation{NodeOperationKind::primary_wheels};
  std::uint64_t implementation_hash{};
  std::uint64_t device_identity{};
  std::string platform;
  std::string device_name;
  std::string driver_version;
  bool native_pipeline_created{};
  bool native_dispatch_completed{};
  bool cpu_gpu_parity{};
  bool no_intermediate_readback{};
  bool cache_reused{};
  bool retirement_invalidated{};
  double max_abs_error{};
  std::string evidence_id;
};

struct NativeNodeOperationSignoff {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  NodeOperationKind operation{NodeOperationKind::primary_wheels};
  NativeNodeSignoffState state{NativeNodeSignoffState::missing};
  std::uint64_t implementation_hash{};
  std::string diagnostic;
};

struct NativeNodeBackendSignoffReport {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  std::vector<NativeNodeOperationSignoff> operations;
  NativeNodeOperationSignoff parallel_mixer;
  NativeNodeOperationSignoff masked_composition;
  bool production_ready{};
  std::string diagnostic;
};

[[nodiscard]] const char* native_node_operation_name(NodeOperationKind) noexcept;
[[nodiscard]] bool validate_native_node_operation_evidence(
    const NativeNodeOperationEvidence&, double max_allowed_error,
    std::string& diagnostic) noexcept;

class NativeNodeProductionSignoff final {
public:
  void mark_implemented(DigitorRendererBackend, NodeOperationKind,
                        std::uint64_t implementation_hash);
  bool record_evidence(const NativeNodeOperationEvidence&, double max_allowed_error,
                       std::string& diagnostic);
  void record_kernel_evidence(const NativeNodeHardwareQualificationEvidence&,
                              double max_allowed_error);
  [[nodiscard]] NativeNodeBackendSignoffReport report(
      DigitorRendererBackend,
      const std::vector<NodeOperationKind>& required_operations,
      std::uint64_t mixer_contract_hash,
      std::uint64_t mask_contract_hash) const;
  void retire_backend(DigitorRendererBackend) noexcept;
  [[nodiscard]] std::string report_json(const NativeNodeBackendSignoffReport&) const;
private:
  struct Impl;
  Impl* impl_{};
public:
  NativeNodeProductionSignoff();
  ~NativeNodeProductionSignoff();
  NativeNodeProductionSignoff(const NativeNodeProductionSignoff&)=delete;
  NativeNodeProductionSignoff& operator=(const NativeNodeProductionSignoff&)=delete;
};

} // namespace digitor
