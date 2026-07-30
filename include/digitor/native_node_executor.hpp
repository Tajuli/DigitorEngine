#pragma once
#include "digitor/production_node_graph.hpp"
#include "digitor/gpu_frame.hpp"
#include "digitor/gpu_matte.hpp"
#include "digitor/digitor.h"
#include <span>
#include <string>
namespace digitor {
class IRenderBackend;
enum class NativeNodeGraphStatus : std::uint32_t {
 ok, invalid_graph, unsupported_parallel_mixer, unsupported_operation, backend_failure
};
struct NativeNodeMaskCapabilities {
 bool hsl_qualifier_matte{};
 bool power_window_matte{};
 bool matte_multiply{};
 bool masked_composite{};
 [[nodiscard]] bool complete() const noexcept {
  return hsl_qualifier_matte && power_window_matte && matte_multiply && masked_composite;
 }
};
struct NativeNodeGraphPreflight {
 bool supported{};
 NodeId node{};
 NodeOperationKind operation{};
 std::string message;
};
struct NativeNodeGraphResult {
 NativeNodeGraphStatus status{NativeNodeGraphStatus::invalid_graph};
 DigitorResult backend_result{DIGITOR_RESULT_INVALID_ARGUMENT};
 ProcessedGpuFramePtr frame;
 NodeId node{};
 NodeOperationKind operation{};
 std::string message;
};
NativeNodeGraphPreflight preflight_native_node_graph(
 const IRenderBackend&, const ProductionNodeGraph&) noexcept;
// Strict GPU-only executor. It never invokes ProductionNodeGraph::render and
// never reads an intermediate frame back to the CPU. Unsupported operations
// are reported explicitly so a selected GPU backend can never silently fall
// back to the reference implementation.
NativeNodeGraphResult execute_native_node_graph(
 IRenderBackend&, const ProductionNodeGraph&, std::span<const Color>,
 std::uint32_t width, std::uint32_t height, std::int64_t timestamp) noexcept;
}
