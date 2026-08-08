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
// Strict GPU-only executor for CPU source pixels. The source is uploaded once
// through the selected backend and every subsequent node remains GPU-resident.
NativeNodeGraphResult execute_native_node_graph(
 IRenderBackend&, const ProductionNodeGraph&, std::span<const Color>,
 std::uint32_t width, std::uint32_t height, std::int64_t timestamp) noexcept;
// Strict GPU-resident overload used by production media decode. The decoder's
// already-imported ProcessedGpuFrame is the graph input, so this path performs
// no source upload, validation readback, or CPU fallback.
NativeNodeGraphResult execute_native_node_graph(
 IRenderBackend&, const ProductionNodeGraph&, const ProcessedGpuFramePtr&,
 std::int64_t timestamp) noexcept;
}
