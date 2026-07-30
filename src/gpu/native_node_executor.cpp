#include "digitor/native_node_executor.hpp"
#include "digitor/native_node_mask_backend.hpp"
#include "gpu/gpu_backend.hpp"

#include <exception>
#include <unordered_map>
#include <vector>

namespace digitor {
namespace {

struct NativeValue {
  ProcessedGpuFramePtr frame;
  bool source{};
};

bool is_mask_operation(NodeOperationKind kind) noexcept {
  return kind == NodeOperationKind::hsl_qualifier ||
         kind == NodeOperationKind::power_window;
}

const char* missing_mask_capability(
    const NativeNodeMaskCapabilities& capabilities) noexcept {
  if (!capabilities.hsl_qualifier_matte)
    return "native HSL matte generation";
  if (!capabilities.power_window_matte)
    return "native Power Window matte generation";
  if (!capabilities.matte_multiply)
    return "native matte multiplication";
  if (!capabilities.masked_composite)
    return "native masked composite";
  return nullptr;
}

} // namespace

NativeNodeGraphPreflight preflight_native_node_graph(
    const IRenderBackend& backend,
    const ProductionNodeGraph& graph) noexcept {
  NativeNodeGraphPreflight result{
      true, 0, NodeOperationKind::primary_wheels, {}};
  try {
    for (const auto id : graph.execution_order()) {
      const auto& node = graph.node(id);
      if (node.kind == ProductionNodeKind::mixer &&
          !backend.supports_native_node_mixer()) {
        result.supported = false;
        result.node = id;
        result.message = "backend does not implement native parallel mixer";
        return result;
      }
      for (const auto& operation : node.operations) {
        if (!operation.enabled) continue;
        result.node = id;
        result.operation = operation.kind;
        if (is_mask_operation(operation.kind)) {
          const auto* mask_backend =
              dynamic_cast<const NativeNodeMaskBackend*>(&backend);
          if (!mask_backend) {
            result.supported = false;
            result.message =
                "backend does not expose the native node-mask interface";
            return result;
          }
          const auto capabilities =
              mask_backend->native_node_mask_capabilities();
          if (const auto* missing = missing_mask_capability(capabilities)) {
            result.supported = false;
            result.message = std::string("backend lacks ") + missing;
            return result;
          }
          continue;
        }
        if (!backend.supports_native_node_operation(operation.kind)) {
          result.supported = false;
          result.message =
              "backend does not implement selected-node native operation";
          return result;
        }
      }
    }
  } catch (const std::exception& error) {
    result.supported = false;
    result.message = error.what();
  } catch (...) {
    result.supported = false;
    result.message = "unknown native graph preflight error";
  }
  return result;
}

NativeNodeGraphResult execute_native_node_graph(
    IRenderBackend& backend,
    const ProductionNodeGraph& graph,
    std::span<const Color> source,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t timestamp) noexcept {
  NativeNodeGraphResult result{};
  const auto preflight = preflight_native_node_graph(backend, graph);
  if (!preflight.supported) {
    result.status =
        preflight.message.find("mixer") != std::string::npos
            ? NativeNodeGraphStatus::unsupported_parallel_mixer
            : NativeNodeGraphStatus::unsupported_operation;
    result.node = preflight.node;
    result.operation = preflight.operation;
    result.backend_result = DIGITOR_RESULT_UNSUPPORTED;
    result.message = preflight.message;
    return result;
  }

  try {
    if (!width || !height || source.size() != std::size_t(width) * height) {
      result.message = "invalid source frame";
      return result;
    }

    std::unordered_map<NodeId, NativeValue> outputs;
    outputs[graph.input_node()] = {{}, true};
    ProcessedGpuFramePtr final_frame;

    auto fail = [&](NativeNodeGraphStatus status,
                    DigitorResult backend_result,
                    NodeId node,
                    NodeOperationKind operation,
                    std::string message) -> bool {
      result.status = status;
      result.backend_result = backend_result;
      result.node = node;
      result.operation = operation;
      result.message = std::move(message);
      return false;
    };

    auto materialize = [&](NativeValue& value,
                           NodeId node,
                           NodeOperationKind operation,
                           const char* reason) -> bool {
      if (value.frame) return true;
      if (!value.source)
        return fail(NativeNodeGraphStatus::backend_failure,
                    DIGITOR_RESULT_INTERNAL_ERROR, node, operation,
                    "native graph value has neither source lineage nor GPU frame");
      const auto identity = PrimaryWheelsParameters::create();
      ProcessedGpuFramePtr uploaded;
      const auto backend_result = backend.process_primary_wheels_gpu(
          source, width, height, timestamp, *identity, uploaded);
      if (backend_result != DIGITOR_RESULT_OK || !uploaded)
        return fail(backend_result == DIGITOR_RESULT_UNSUPPORTED
                        ? NativeNodeGraphStatus::unsupported_operation
                        : NativeNodeGraphStatus::backend_failure,
                    backend_result, node, operation, reason);
      value = {std::move(uploaded), false};
      return true;
    };

    auto process_color_operation = [&](NativeValue& current,
                                       NodeId node,
                                       const NodeOperation& operation) -> bool {
      ProcessedGpuFramePtr next;
      DigitorResult backend_result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      const bool first = !current.frame && current.source;
      switch (operation.kind) {
        case NodeOperationKind::primary_wheels: {
          const auto parameters =
              std::get<std::shared_ptr<const PrimaryWheelsParameters>>(
                  operation.payload);
          if (!parameters)
            return fail(NativeNodeGraphStatus::backend_failure,
                        DIGITOR_RESULT_INVALID_ARGUMENT, node, operation.kind,
                        "primary wheels operation has no parameters");
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
          if (!parameters)
            return fail(NativeNodeGraphStatus::backend_failure,
                        DIGITOR_RESULT_INVALID_ARGUMENT, node, operation.kind,
                        "log wheels operation has no parameters");
          backend_result = first
              ? backend.process_log_wheels_gpu(
                    source, width, height, timestamp, *parameters, next)
              : backend.process_log_wheels_gpu(
                    backend.gpu_source(current.frame), timestamp, *parameters,
                    next);
          break;
        }
        case NodeOperationKind::rgb_curves: {
          const auto parameters =
              std::get<std::shared_ptr<const CompiledRgbCurves>>(
                  operation.payload);
          if (!parameters)
            return fail(NativeNodeGraphStatus::backend_failure,
                        DIGITOR_RESULT_INVALID_ARGUMENT, node, operation.kind,
                        "RGB curves operation has no compiled parameters");
          backend_result = first
              ? backend.process_curves_gpu(
                    source, width, height, timestamp, *parameters, next)
              : backend.process_curves_gpu(
                    backend.gpu_source(current.frame), timestamp, *parameters,
                    next);
          break;
        }
        case NodeOperationKind::hsl_qualifier:
        case NodeOperationKind::power_window:
          return fail(NativeNodeGraphStatus::backend_failure,
                      DIGITOR_RESULT_INTERNAL_ERROR, node, operation.kind,
                      "mask operation entered the color-operation executor");
        default:
          if (first &&
              !materialize(current, node, operation.kind,
                           "backend-native source upload failed before selected-node pass"))
            return false;
          backend_result = backend.process_node_operation_gpu(
              backend.gpu_source(current.frame), timestamp, operation, next);
          break;
      }
      if (backend_result != DIGITOR_RESULT_OK || !next)
        return fail(backend_result == DIGITOR_RESULT_UNSUPPORTED
                        ? NativeNodeGraphStatus::unsupported_operation
                        : NativeNodeGraphStatus::backend_failure,
                    backend_result, node, operation.kind,
                    backend_result == DIGITOR_RESULT_UNSUPPORTED
                        ? "backend does not implement selected-node native pass"
                        : "backend-native node pass failed");
      current = {std::move(next), false};
      return true;
    };

    for (const auto id : graph.execution_order()) {
      const auto& node = graph.node(id);
      if (node.kind == ProductionNodeKind::input) continue;

      if (node.kind == ProductionNodeKind::output) {
        if (node.inputs.size() != 1 || !outputs.contains(node.inputs.front())) {
          result.node = id;
          result.message = "output node has no native input";
          return result;
        }
        auto value = outputs.at(node.inputs.front());
        if (!materialize(value, id, NodeOperationKind::primary_wheels,
                         "backend-native source upload failed for final output"))
          return result;
        final_frame = std::move(value.frame);
        break;
      }

      if (node.kind == ProductionNodeKind::mixer) {
        std::vector<GpuSourceResource> sources;
        sources.reserve(node.inputs.size());
        for (const auto input : node.inputs) {
          if (!outputs.contains(input)) {
            result.node = id;
            result.message = "mixer input is unavailable";
            return result;
          }
          auto& value = outputs.at(input);
          if (!materialize(value, id, NodeOperationKind::primary_wheels,
                           "backend-native source upload failed for parallel branch"))
            return result;
          auto gpu = backend.gpu_source(value.frame);
          if (gpu.readiness != GpuReadiness::Ready) {
            fail(NativeNodeGraphStatus::backend_failure,
                 DIGITOR_RESULT_INVALID_ARGUMENT, id,
                 NodeOperationKind::primary_wheels,
                 "parallel mixer received unusable native source");
            return result;
          }
          sources.push_back(std::move(gpu));
        }
        ProcessedGpuFramePtr mixed;
        const auto backend_result =
            backend.mix_gpu_sources(sources, timestamp, mixed);
        if (backend_result != DIGITOR_RESULT_OK || !mixed) {
          fail(NativeNodeGraphStatus::backend_failure, backend_result, id,
               NodeOperationKind::primary_wheels,
               "backend-native parallel mixer failed");
          return result;
        }
        outputs[id] = {std::move(mixed), false};
        continue;
      }

      if (node.inputs.size() != 1 || !outputs.contains(node.inputs.front())) {
        result.node = id;
        result.message = "grade node must have exactly one available input";
        return result;
      }

      NativeValue original = outputs.at(node.inputs.front());
      if (!node.enabled || node.bypassed || node.operations.empty()) {
        outputs[id] = std::move(original);
        continue;
      }

      std::vector<const NodeOperation*> masks;
      bool has_color_operation = false;
      for (const auto& operation : node.operations) {
        if (!operation.enabled) continue;
        if (is_mask_operation(operation.kind))
          masks.push_back(&operation);
        else
          has_color_operation = true;
      }

      if (!masks.empty() &&
          !materialize(original, id, masks.front()->kind,
                       "backend-native source upload failed before mask generation"))
        return result;

      NativeValue current = original;
      if (!has_color_operation && masks.empty()) {
        outputs[id] = std::move(current);
        continue;
      }

      for (const auto& operation : node.operations) {
        if (!operation.enabled || is_mask_operation(operation.kind)) continue;
        if (!process_color_operation(current, id, operation)) return result;
      }

      if (!masks.empty()) {
        auto* mask_backend = dynamic_cast<NativeNodeMaskBackend*>(&backend);
        if (!mask_backend) {
          fail(NativeNodeGraphStatus::backend_failure,
               DIGITOR_RESULT_INTERNAL_ERROR, id, masks.front()->kind,
               "native mask backend disappeared after successful preflight");
          return result;
        }
        if (!materialize(current, id, masks.front()->kind,
                         "backend-native source upload failed before masked composite"))
          return result;

        const auto original_source = backend.gpu_source(original.frame);
        const auto processed_source = backend.gpu_source(current.frame);
        if (original_source.readiness != GpuReadiness::Ready ||
            processed_source.readiness != GpuReadiness::Ready) {
          fail(NativeNodeGraphStatus::backend_failure,
               DIGITOR_RESULT_INVALID_ARGUMENT, id, masks.front()->kind,
               "masked node received unusable native frame resources");
          return result;
        }

        std::vector<GpuMatteResourcePtr> mattes;
        mattes.reserve(masks.size());
        for (const auto* operation : masks) {
          GpuMatteResourcePtr matte;
          DigitorResult backend_result = DIGITOR_RESULT_UNSUPPORTED;
          if (operation->kind == NodeOperationKind::hsl_qualifier) {
            const auto parameters =
                std::get<std::shared_ptr<const HslQualifierParameters>>(
                    operation->payload);
            if (!parameters) {
              fail(NativeNodeGraphStatus::backend_failure,
                   DIGITOR_RESULT_INVALID_ARGUMENT, id, operation->kind,
                   "HSL qualifier mask has no parameters");
              return result;
            }
            backend_result = mask_backend->generate_hsl_matte(
                original_source, timestamp, *parameters, matte);
          } else {
            backend_result = mask_backend->generate_power_window_matte(
                width, height, timestamp,
                std::get<PowerWindowSettings>(operation->payload), matte);
          }
          if (backend_result != DIGITOR_RESULT_OK || !matte ||
              !matte->usable_by(original_source.backend,
                                original_source.context_identity)) {
            fail(backend_result == DIGITOR_RESULT_UNSUPPORTED
                     ? NativeNodeGraphStatus::unsupported_operation
                     : NativeNodeGraphStatus::backend_failure,
                 backend_result, id, operation->kind,
                 "backend-native matte generation failed");
            return result;
          }
          const auto& metadata = matte->metadata();
          if (metadata.width != width || metadata.height != height ||
              metadata.format != GpuMatteFormat::r32_float) {
            fail(NativeNodeGraphStatus::backend_failure,
                 DIGITOR_RESULT_INTERNAL_ERROR, id, operation->kind,
                 "backend returned an invalid node-local matte resource");
            return result;
          }
          mattes.push_back(std::move(matte));
        }

        GpuMatteResourcePtr combined = mattes.front();
        if (mattes.size() > 1) {
          combined.reset();
          const auto backend_result = mask_backend->multiply_mattes(
              mattes, timestamp, combined);
          if (backend_result != DIGITOR_RESULT_OK || !combined ||
              !combined->usable_by(original_source.backend,
                                   original_source.context_identity)) {
            fail(backend_result == DIGITOR_RESULT_UNSUPPORTED
                     ? NativeNodeGraphStatus::unsupported_operation
                     : NativeNodeGraphStatus::backend_failure,
                 backend_result, id, masks.front()->kind,
                 "backend-native matte multiplication failed");
            return result;
          }
        }

        ProcessedGpuFramePtr composited;
        const auto backend_result = mask_backend->composite_with_matte(
            original_source, processed_source, combined, timestamp,
            composited);
        if (backend_result != DIGITOR_RESULT_OK || !composited) {
          fail(backend_result == DIGITOR_RESULT_UNSUPPORTED
                   ? NativeNodeGraphStatus::unsupported_operation
                   : NativeNodeGraphStatus::backend_failure,
               backend_result, id, masks.front()->kind,
               "backend-native masked composite failed");
          return result;
        }
        current = {std::move(composited), false};
      }

      outputs[id] = std::move(current);
    }

    if (!final_frame) {
      result.status = NativeNodeGraphStatus::backend_failure;
      result.backend_result = DIGITOR_RESULT_INTERNAL_ERROR;
      result.message = "graph produced no final native GPU frame";
      return result;
    }
    result.status = NativeNodeGraphStatus::ok;
    result.backend_result = DIGITOR_RESULT_OK;
    result.frame = std::move(final_frame);
    result.message =
        "native GPU-only DAG execution completed with node-local mask composition";
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
