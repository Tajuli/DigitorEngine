#pragma once
#include "digitor/node_graph.hpp"
#include "digitor/primary_wheels.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/rgb_curves.hpp"
#include "digitor/qualifier.hpp"
#include "digitor/correction.hpp"
#include "digitor/lut.hpp"
#include "digitor/effects.hpp"
#include <memory>
#include <optional>
#include <variant>

namespace digitor {
enum class ProductionNodeKind : std::uint32_t { input, serial, parallel, mixer, output };
enum class NodeOperationKind : std::uint32_t { primary_wheels, log_wheels, rgb_curves, hsl_qualifier, correction, lut1d, lut3d, effect, power_window };
enum class WindowShape : std::uint32_t { rectangle, ellipse, linear_gradient };
struct NodePosition { float x{}, y{}; };
struct PowerWindowSettings { WindowShape shape{WindowShape::ellipse}; float center_x{.5f},center_y{.5f},width{1},height{1},rotation{},feather{.1f},opacity{1}; bool invert{}; };
using NodeOperationPayload=std::variant<std::shared_ptr<const PrimaryWheelsParameters>,std::shared_ptr<const LogWheelsParameters>,std::shared_ptr<const CompiledRgbCurves>,std::shared_ptr<const HslQualifierParameters>,std::shared_ptr<const CorrectionParameters>,std::shared_ptr<const Lut1D>,std::shared_ptr<const Lut3D>,EffectSettings,PowerWindowSettings>;
struct NodeOperation { NodeOperationKind kind{}; NodeOperationPayload payload; bool enabled{true}; std::string identity; };
struct NodeRenderStats { std::size_t executed_nodes{}; std::size_t cache_hits{}; std::size_t cache_misses{}; std::size_t cache_evictions{}; std::size_t cache_bytes{}; };
struct ProductionNode { NodeId id{}; ProductionNodeKind kind{ProductionNodeKind::serial}; std::string name; std::vector<NodeId> inputs; std::vector<NodeOperation> operations; bool enabled{true}; bool bypassed{}; NodePosition position{}; };

class ProductionNodeGraph final {
public:
 ProductionNodeGraph();
 NodeId input_node()const noexcept{return input_;} NodeId output_node()const noexcept{return output_;}
 NodeId selected_node()const noexcept{return selected_;} void select_node(NodeId);
 NodeId add_serial_after(NodeId,std::string name="Serial Node");
 std::pair<NodeId,NodeId> add_parallel_after(NodeId,std::string first="Parallel A",std::string second="Parallel B");
 NodeId convert_to_parallel(NodeId existing,std::string new_branch="Parallel Node");
 void remove_node(NodeId); void connect(NodeId,NodeId); void disconnect(NodeId,NodeId);
 void set_enabled(NodeId,bool); void set_bypassed(NodeId,bool); void set_position(NodeId,NodePosition);
 void add_operation_to_selected(NodeOperation);
 void set_operation_on_selected(NodeOperation);
 bool remove_operation(NodeId,NodeOperationKind);
 bool set_operation_enabled(NodeId,NodeOperationKind,bool);
 void clear_operations(NodeId);
 const ProductionNode& node(NodeId)const; std::vector<NodeId> execution_order()const;
 NodeValue render(std::span<const Color>,std::uint32_t width,std::uint32_t height,std::int64_t frame=0)const;
 NodeValue render_cached(std::span<const Color>,std::uint32_t width,std::uint32_t height,std::uint64_t source_generation,NodeRenderStats* stats=nullptr,std::int64_t frame=0)const;
 void clear_render_cache()const; std::size_t cached_node_count()const noexcept{return render_cache_.size();}
 void set_render_cache_budget_bytes(std::size_t bytes)const; std::size_t render_cache_budget_bytes()const noexcept{return render_cache_budget_bytes_;} std::size_t render_cache_bytes()const noexcept{return render_cache_bytes_;}
 std::string recipe_identity()const; std::string to_json()const; void serialize(std::ostream&)const;
 std::size_t size()const noexcept{return nodes_.size();}
private:
 void normalize_mixers();
 std::vector<NodeId> consumers(NodeId)const;
 NodeValue apply_operations(const ProductionNode&,NodeValue,std::uint32_t,std::uint32_t)const;
 static NodeValue mix_inputs(const std::vector<NodeValue>&);
 struct CachedNodeValue { std::string key; NodeValue value; std::uint64_t last_use{}; std::size_t bytes{}; };
 void trim_render_cache(NodeId protected_id,NodeRenderStats* stats)const;
 std::unordered_map<NodeId,ProductionNode> nodes_; NodeId next_{1},input_{},output_{},selected_{};
 mutable std::unordered_map<NodeId,CachedNodeValue> render_cache_;
 mutable std::size_t render_cache_budget_bytes_{256u*1024u*1024u};
 mutable std::size_t render_cache_bytes_{};
 mutable std::uint64_t render_cache_clock_{};
};
NodeOperation make_primary_wheels_operation(std::shared_ptr<const PrimaryWheelsParameters>);
NodeOperation make_log_wheels_operation(std::shared_ptr<const LogWheelsParameters>);
NodeOperation make_rgb_curves_operation(std::shared_ptr<const CompiledRgbCurves>);
NodeOperation make_hsl_qualifier_operation(std::shared_ptr<const HslQualifierParameters>);
NodeOperation make_correction_operation(std::shared_ptr<const CorrectionParameters>);
NodeOperation make_lut_operation(std::shared_ptr<const Lut1D>,LutInterpolation=LutInterpolation::linear);
NodeOperation make_lut_operation(std::shared_ptr<const Lut3D>,LutInterpolation=LutInterpolation::tetrahedral);
NodeOperation make_effect_operation(EffectSettings);
NodeOperation make_power_window_operation(PowerWindowSettings);
}
