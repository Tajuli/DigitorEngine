#include "digitor/native_node_executor.hpp"
#include "gpu/gpu_backend.hpp"
#include <exception>
#include <unordered_map>
namespace digitor {
NativeNodeGraphPreflight preflight_native_node_graph(const IRenderBackend& backend, const ProductionNodeGraph& graph) noexcept {
 NativeNodeGraphPreflight out{true,0,NodeOperationKind::primary_wheels,{}};
 try {
  for (const auto id : graph.execution_order()) {
   const auto& n = graph.node(id);
   if (n.kind == ProductionNodeKind::mixer && !backend.supports_native_node_mixer()) {
    out.supported=false; out.node=id; out.message="backend does not implement native parallel mixer"; return out;
   }
   for (const auto& op : n.operations) {
    if (op.enabled && !backend.supports_native_node_operation(op.kind)) {
     out.supported=false; out.node=id; out.operation=op.kind; out.message="backend does not implement selected-node native operation"; return out;
    }
   }
  }
 } catch (const std::exception& e) { out.supported=false; out.message=e.what(); }
 catch (...) { out.supported=false; out.message="unknown native graph preflight error"; }
 return out;
}
NativeNodeGraphResult execute_native_node_graph(IRenderBackend& backend,
 const ProductionNodeGraph& graph,std::span<const Color> source,
 std::uint32_t width,std::uint32_t height,std::int64_t timestamp) noexcept {
 NativeNodeGraphResult r{};
 const auto preflight=preflight_native_node_graph(backend,graph);
 if(!preflight.supported){r.status=preflight.message.find("mixer")!=std::string::npos?NativeNodeGraphStatus::unsupported_parallel_mixer:NativeNodeGraphStatus::unsupported_operation;r.node=preflight.node;r.operation=preflight.operation;r.backend_result=DIGITOR_RESULT_UNSUPPORTED;r.message=preflight.message;return r;}
 try {
  if(width==0||height==0||source.size()!=static_cast<std::size_t>(width)*height){r.message="invalid source frame";return r;}
  std::unordered_map<NodeId,ProcessedGpuFramePtr> outputs;
  ProcessedGpuFramePtr last;
  for(const auto id:graph.execution_order()){
   const auto& n=graph.node(id);
   if(n.kind==ProductionNodeKind::input)continue;
   if(n.kind==ProductionNodeKind::output){
    if(n.inputs.size()!=1||!outputs.count(n.inputs.front())){r.message="output node has no native input";return r;}
    last=outputs.at(n.inputs.front());break;
   }
   if(n.kind==ProductionNodeKind::mixer){
    std::vector<GpuSourceResource> sources;
    for(auto input:n.inputs){auto it=outputs.find(input);if(it==outputs.end()||!it->second){r.node=id;r.message="mixer input is unavailable";return r;}sources.push_back(backend.gpu_source(it->second));}
    ProcessedGpuFramePtr mixed;auto dr=backend.mix_gpu_sources(sources,timestamp,mixed);
    if(dr!=DIGITOR_RESULT_OK||!mixed){r.status=NativeNodeGraphStatus::backend_failure;r.backend_result=dr;r.node=id;r.message="backend-native parallel mixer failed";return r;}
    outputs[id]=std::move(mixed);continue;
   }
   if(n.inputs.size()!=1){r.node=id;r.message="grade node must have exactly one input";return r;}
   ProcessedGpuFramePtr current;
   auto predecessor=outputs.find(n.inputs.front());if(predecessor!=outputs.end())current=predecessor->second;
   if(!n.enabled||n.bypassed){if(current)outputs[id]=current;continue;}
   for(const auto& op:n.operations){
    if(!op.enabled)continue;
    ProcessedGpuFramePtr next;DigitorResult dr=DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    const bool first=!current;
    switch(op.kind){
     case NodeOperationKind::primary_wheels:{auto p=std::get<std::shared_ptr<const PrimaryWheelsParameters>>(op.payload);dr=first?backend.process_primary_wheels_gpu(source,width,height,timestamp,*p,next):backend.process_primary_wheels_gpu(backend.gpu_source(current),timestamp,*p,next);break;}
     case NodeOperationKind::log_wheels:{auto p=std::get<std::shared_ptr<const LogWheelsParameters>>(op.payload);dr=first?backend.process_log_wheels_gpu(source,width,height,timestamp,*p,next):backend.process_log_wheels_gpu(backend.gpu_source(current),timestamp,*p,next);break;}
     case NodeOperationKind::rgb_curves:{auto p=std::get<std::shared_ptr<const CompiledRgbCurves>>(op.payload);dr=first?backend.process_curves_gpu(source,width,height,timestamp,*p,next):backend.process_curves_gpu(backend.gpu_source(current),timestamp,*p,next);break;}
     case NodeOperationKind::hsl_qualifier:{auto p=std::get<std::shared_ptr<const HslQualifierParameters>>(op.payload);dr=first?backend.process_hsl_qualifier_gpu(source,width,height,timestamp,*p,next):backend.process_hsl_qualifier_gpu(backend.gpu_source(current),timestamp,*p,next);break;}
     default:{
      if(first){r.status=NativeNodeGraphStatus::unsupported_operation;r.node=id;r.operation=op.kind;r.message="first native node operation requires a source-upload capable pass";return r;}
      dr=backend.process_node_operation_gpu(backend.gpu_source(current),timestamp,op,next);break;
     }
    }
    if(dr!=DIGITOR_RESULT_OK||!next){r.status=dr==DIGITOR_RESULT_UNSUPPORTED?NativeNodeGraphStatus::unsupported_operation:NativeNodeGraphStatus::backend_failure;r.backend_result=dr;r.node=id;r.operation=op.kind;r.message=dr==DIGITOR_RESULT_UNSUPPORTED?"backend does not implement selected-node native pass":"backend-native node pass failed";return r;}
    current=std::move(next);
   }
   if(current)outputs[id]=current;
  }
  if(!last){r.status=NativeNodeGraphStatus::unsupported_operation;r.message="graph produced no final native GPU frame";return r;}
  r.status=NativeNodeGraphStatus::ok;r.backend_result=DIGITOR_RESULT_OK;r.frame=std::move(last);r.message="native GPU-only DAG execution completed";return r;
 }catch(const std::exception&e){r.message=e.what();return r;}catch(...){r.message="unknown native graph error";return r;}
}
}
