#pragma once
#include "digitor/native_node_hardware_qualification.hpp"
#include "digitor/native_node_kernels.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace digitor {

struct NativeNodeHardwareHarnessIdentity {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  std::uint64_t device_identity{};
  std::string platform;
  std::string device_name;
  std::string driver_version;
  std::string evidence_id_prefix{"native-node"};
};

struct NativeNodeHardwareHarnessCallbacks {
  std::function<bool(NativeNodeKernel, std::uint32_t, std::uint32_t, std::string&)> prepare;
  std::function<bool(NativeNodeKernel, std::uint32_t, std::uint32_t,
                     std::span<const Color>, std::span<const Color>,
                     std::span<const float>, std::span<Color>, std::string&)> execute_and_readback;
  std::function<std::uint64_t()> pipeline_create_count;
  std::function<std::uint64_t()> pipeline_cache_hit_count;
  std::function<std::uint64_t()> intermediate_cpu_readback_count;
  std::function<void(std::uint64_t)> retire_device;
  std::function<bool(NativeNodeKernel, std::string&)> verify_retired;
};

struct NativeNodeHardwareHarnessResult {
  NativeNodeHardwareQualificationEvidence evidence;
  bool passed{};
  std::string diagnostic;
};

[[nodiscard]] NativeNodeHardwareHarnessResult run_native_node_hardware_qualification(
    const NativeNodeHardwareHarnessIdentity&,
    const NativeNodeHardwareHarnessCallbacks&,
    NativeNodeKernel kernel,
    std::uint32_t width = 8,
    std::uint32_t height = 8,
    double tolerance = 1.0 / 255.0) noexcept;

} // namespace digitor
