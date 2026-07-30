#pragma once
#include "digitor/digitor.h"
#include "digitor/native_node_shader_contracts.hpp"
#include <cstdint>
#include <string>
namespace digitor {
enum class NativeNodeQualificationState : std::uint32_t { unavailable, implemented_unqualified, qualified };
struct NativeNodeQualificationRecord {
 DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
 NativeNodeKernel kernel{NativeNodeKernel::parallel_mixer};
 NativeNodeQualificationState state{NativeNodeQualificationState::unavailable};
 std::uint64_t contract_hash{};
 std::string evidence;
};
class NativeNodeQualificationRegistry {
public:
 void record(NativeNodeQualificationRecord);
 [[nodiscard]] NativeNodeQualificationRecord query(DigitorRendererBackend,NativeNodeKernel) const;
 [[nodiscard]] bool production_ready(DigitorRendererBackend,NativeNodeKernel,std::uint64_t expected_hash) const;
 void retire_backend(DigitorRendererBackend) noexcept;
private:
 struct Impl; Impl* impl_;
public:
 NativeNodeQualificationRegistry(); ~NativeNodeQualificationRegistry();
 NativeNodeQualificationRegistry(const NativeNodeQualificationRegistry&)=delete;
 NativeNodeQualificationRegistry& operator=(const NativeNodeQualificationRegistry&)=delete;
};
[[nodiscard]] bool validate_native_node_qualification(const NativeNodeQualificationRecord&) noexcept;
}
