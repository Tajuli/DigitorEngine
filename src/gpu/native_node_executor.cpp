#include "digitor/native_node_executor.hpp"
#include "gpu/gpu_backend.hpp"
#include <exception>
#include <unordered_map>

namespace digitor {
namespace {
struct NativeNodeValue {
  ProcessedGpuFramePtr frame;
  bool source_backed{};
};

bool is_node_mask(NodeOperationKind kind) noexcept {
  return kind == NodeOperationKind::hsl_qualifier ||
         kind == NodeOperationKind::power_window;
}
}

NativeNodeGraphPreflight preflight_native_node_graph(
    const IRenderBackend& backend, const ProductionNodeGraph& graph) noexcept {
  NativeNodeGraphPreflight out{true, 0, NodeOperationKind::primary_wheels, {}};
  try {
    for (const auto id : graph.execution_order()) {
      const auto& node = graph.node(id);
      if (node.kind == ProductionNodeKind::mixer &&
          !backend.supports_native_node_mixer()) {
        out.supported = false;
        out.node = id;
        out.message = "backend does not implement native parallel mixer";
        return out;
      }

      bool has_enabled_mask = false;
      bool has_enabled_processing = false;
      for (const auto& operation : node.operations) {
        if (!operation.enabled) continue;
        if (!backend.supports_native_node_operation(operation.kind)) {
          out.supported = false;
          out.node = id;
          out.operation = operation.kind;
          out.message = "backend does not implement selected-node native operation";
          return out;
        }
        if (is_node_mask(operation.kind)) has_enabled_mask = true;
        else has_enabled_processing = true;
      }

      // HSL qualifier and Power Window are node-local mattes. The current
      // backend contract can produce a matte and can run color passes, but it
      // cannot yet bind original + processed + matte as one native composite.
      // Reject this graph before execution rather than treating matte pixels as
      // an RGBA color frame or silently falling back to CPU.
      if (has_enabled_mask) {
        out.supported = false;
        out.node = id;
        out.operation = NodeOperationKind::hsl_qualifier;
        out.message = has_enabled_processing
            ? "backend does not implement native node-local mask composition"
            : "node-local mask requires a following processing operation";
        return out;
      }
    }
  } catch (const std::exception& error) {
    out.supported = false;
    out.message = error.what();
  } catch (...) {
    out.supported = false;
    out.message = "unknown native graph preflight error";
  }
  return out;
}

NativeNodeGraphResult execute_native_node_graph(
    IRenderBackend& backend, const ProductionNodeGraph& graph,
    std::span<const Color> source, std::uint32_t width, std::uint32_t height,
    std::int64_t timestamp) noexcept {
  NativeNodeGraphResult result{};
  const auto preflight = preflight_native_node_graph(backend, graph);
  if (!preflight.supported) {
    result.status = preflight.message.find("mixer") != std::string::npos
        ? NativeNodeGraphStatus::unsupported_parallel_mixer
        : NativeNodeGraphStatus::unsupported_operation;
    result.node = preflight.node;
    result.operation = preflight.operation;
    result.backend_result = DIGITOR_RESULT_UNSUPPORTED;
    result.message = preflight.message;
    return result;
  }

  try {
    if (width == 0 || height == 0 ||
        source.size() != static_cast<std::size_t>(width) * height) {
      result.message = "invalid source frame";
      return result;
    }

    std::unordered_map<NodeId, NativeNodeValue> outputs;
    outputs.emplace(graph.input_node(), NativeNodeValue{{}, true});
    ProcessedGpuFramePtr last;

    for (const auto id : graph.execution_order()) {
      const auto& node = graph.node(id);
      if (node.kind == ProductionNodeKind::input) continue;

      if (node.kind == ProductionNodeKind::output) {
        if (node.inputs.size() != 1 || !outputs.count(node.inputs.front())) {
          result.message = "output node has no native input";
          return result;
        }
        const auto& final_value = outputs.at(node.inputs.front());
        if (!final_value.frame) {
          result.status = NativeNodeGraphStatus::unsupported_operation;
          result.message = final_value.source_backed
              ? "graph contains no native processing pass to materialize the source"
              : "graph produced no final native GPU frame";
          return result;
        }
        last = final_value.frame;
        break;
      }

      if (node.kind == ProductionNodeKind::mixer) {
        std::vector<GpuSourceResource> sources;
        for (const auto input : node.inputs) {
          const auto iterator = outputs.find(input);
          if (iterator == outputs.end() || !iterator->second.frame) {
            result.node = id;
            result.message = iterator != outputs.end() &&
                                     iterator->second.source_backed
                ? "parallel mixer input requires a native materialized frame"
                : "mixer input is unavailable";
            return result;
          }
          sources.push_back(backend.gpu_source(iterator->second.frame));
        }
        ProcessedGpuFramePtr mixed;
        const auto backend_result =
            backend.mix_gpu_sources(sources, timestamp, mixed);
        if (backend_result != DIGITOR_RESULT_OK || !mixed) {
          result.status = NativeNodeGraphStatus::backend_failure;
          result.backend_result = backend_result;
          result.node = id;
          result.message = "backend-native parallel mixer failed";
          return result;
        }
        outputs[id] = NativeNodeValue{std::move(mixed), false};
        continue;
      }

      if (node.inputs.size() != 1) {
        result.node = id;
        result.message = "grade node must have exactly one input";
        return result;
      }

      const auto predecessor = outputs.find(node.inputs.front());
      if (predecessor == outputs.end()) {
        result.node = id;
        result.message = "grade node input is unavailable";
        return result;
      }
      NativeNodeValue current = predecessor->second;

      // Empty, disabled, and bypassed nodes are aliases. Preserve whether the
      // value is still backed by the original CPU source so a later native pass
      // can perform the one required upload.
      if (!node.enabled || node.bypassed || node.operations.empty()) {
        outputs[id] = current;
        continue;
      }

      for (const auto& operation : node.operations) {
        if (!operation.enabled) continue;
        ProcessedGpuFramePtr next;
        DigitorResult backend_result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        const bool first = !current.frame && current.source_backed;

        switch (operation.kind) {
        case NodeOperationKind::primary_wheels: {
          const auto parameters =
              std::get<std::shared_ptr<const PrimaryWheelsParameters>>(
                  operation.payload);
          backend_result = first
              ? backend.process_primary_wheels_gpu(
                    source, width, height, timestamp, *parameters, next)
              : backend.process_primary_wheels_gpu(
                    backend.gpu_source(current.frame), timestamp, *parameters,
                    next);
          break;
        }
        case NodeOperationKind::log_wheels: {
          const auto parameters =
              std::get<std::shared_ptr<const LogWheelsParameters>>(
                  operation.payload);
          backend_result = first
              ? backend.process_log_wheels_gpu(source, width, height, timestamp,
                                               *parameters, next)
              : backend.process_log_wheels_gpu(
                    backend.gpu_source(current.frame), timestamp, *parameters,
                    next);
          break;
        }
        case NodeOperationKind::rgb_curves: {
          const auto parameters =
              std::get<std::shared_ptr<const CompiledRgbCurves>>(
                  operation.payload);
          backend_result = first
              ? backend.process_curves_gpu(source, width, height, timestamp,
                                           *parameters, next)
              : backend.process_curves_gpu(backend.gpu_source(current.frame),
                                           timestamp, *parameters, next);
          break;
        }
        case NodeOperationKind::hsl_qualifier:
        case NodeOperationKind::power_window:
          result.status = NativeNodeGraphStatus::unsupported_operation;
          result.backend_result = DIGITOR_RESULT_UNSUPPORTED;
          result.node = id;
          result.operation = operation.kind;
          result.message =
              "node-local mask reached execution after successful preflight";
          return result;
        default: {
          if (first) {
            result.status = NativeNodeGraphStatus::unsupported_operation;
            result.node = id;
            result.operation = operation.kind;
            result.message =
                "first native node operation requires a source-upload capable pass";
            return result;
          }
          backend_result = backend.process_node_operation_gpu(
              backend.gpu_source(current.frame), timestamp, operation, next);
          break;
        }
        }

        if (backend_result != DIGITOR_RESULT_OK || !next) {
          result.status = backend_result == DIGITOR_RESULT_UNSUPPORTED
              ? NativeNodeGraphStatus::unsupported_operation
              : NativeNodeGraphStatus::backend_failure;
          result.backend_result = backend_result;
          result.node = id;
          result.operation = operation.kind;
          result.message = backend_result == DIGITOR_RESULT_UNSUPPORTED
              ? "backend does not implement selected-node native pass"
              : "backend-native node pass failed";
          return result;
        }
        current = NativeNodeValue{std::move(next), false};
      }
      outputs[id] = std::move(current);
    }

    if (!last) {
      result.status = NativeNodeGraphStatus::unsupported_operation;
      result.message = "graph produced no final native GPU frame";
      return result;
    }
    result.status = NativeNodeGraphStatus::ok;
    result.backend_result = DIGITOR_RESULT_OK;
    result.frame = std::move(last);
    result.message = "native GPU-only DAG execution completed";
    return result;
  } catch (const std::exception& error) {
    result.message = error.what();
    return result;
  } catch (...) {
    result.message = "unknown native graph error";
    return result;
  }
}
} // namespace digitor
