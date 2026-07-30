#include "digitor/production_node_graph.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace digitor { namespace {
std::string opid(NodeOperationKind k,const std::string&s){return std::to_string(static_cast<unsigned>(k))+":"+s;}
float window_matte(const PowerWindowSettings&w,float x,float y){float dx=x-w.center_x,dy=y-w.center_y;float c=std::cos(-w.rotation),s=std::sin(-w.rotation);float rx=c*dx-s*dy,ry=s*dx+c*dy;float d=0; if(w.shape==WindowShape::ellipse)d=std::sqrt((rx/(w.width*.5f))*(rx/(w.width*.5f))+(ry/(w.height*.5f))*(ry/(w.height*.5f)));else if(w.shape==WindowShape::rectangle)d=std::max(std::abs(rx)/(w.width*.5f),std::abs(ry)/(w.height*.5f));else d=(rx/(std::max(w.width,.0001f))+.5f);float m=1-std::clamp((d-1+std::max(w.feather,.0001f))/std::max(w.feather,.0001f),0.f,1.f);if(w.shape==WindowShape::linear_gradient)m=std::clamp(1-d,0.f,1.f);if(w.invert)m=1-m;return std::clamp(m*w.opacity,0.f,1.f);}
Color blend(Color a,Color b,float m){return{a.r+(b.r-a.r)*m,a.g+(b.g-a.g)*m,a.b+(b.b-a.b)*m,a.a};}
}
ProductionNodeGraph::ProductionNodeGraph(){input_=next_++;output_=next_++;nodes_[input_]={input_,ProductionNodeKind::input,"Input",{}, {},true,false,{0,0}};nodes_[output_]={output_,ProductionNodeKind::output,"Output",{input_},{},true,false,{600,0}};selected_=0;}
const ProductionNode&ProductionNodeGraph::node(NodeId id)const{auto i=nodes_.find(id);if(i==nodes_.end())throw std::out_of_range("production node");return i->second;}
void ProductionNodeGraph::select_node(NodeId id){auto&n=nodes_.at(id);if(n.kind==ProductionNodeKind::input||n.kind==ProductionNodeKind::output||n.kind==ProductionNodeKind::mixer)throw std::invalid_argument("node is not grade-selectable");selected_=id;}
void ProductionNodeGraph::set_position(NodeId id,NodePosition p){auto&n=nodes_.at(id);n.position=p;}
std::vector<NodeId> ProductionNodeGraph::consumers(NodeId id)const{std::vector<NodeId> out;for(auto&[nid,n]:nodes_)if(std::find(n.inputs.begin(),n.inputs.end(),id)!=n.inputs.end())out.push_back(nid);return out;}
void ProductionNodeGraph::connect(NodeId a,NodeId b){node(a);auto&i=nodes_.at(b);if(a==b||std::find(i.inputs.begin(),i.inputs.end(),a)!=i.inputs.end())throw std::invalid_argument("invalid connection");i.inputs.push_back(a);try{execution_order();}catch(...){i.inputs.pop_back();throw;}}
void ProductionNodeGraph::disconnect(NodeId a,NodeId b){auto&v=nodes_.at(b).inputs;auto it=std::find(v.begin(),v.end(),a);if(it==v.end())throw std::invalid_argument("missing connection");v.erase(it);}
NodeId ProductionNodeGraph::add_serial_after(NodeId after,std::string name){node(after);NodeId id=next_++;nodes_[id]={id,ProductionNodeKind::serial,std::move(name),{after},{},true,false,{node(after).position.x+160,node(after).position.y}};for(auto&[nid,n]:nodes_)if(nid!=id)for(auto&x:n.inputs)if(x==after)x=id;selected_=id;return id;}
std::pair<NodeId,NodeId>ProductionNodeGraph::add_parallel_after(NodeId after,std::string a,std::string b){node(after);NodeId n1=next_++,n2=next_++,mix=next_++;auto base=node(after).position;nodes_[n1]={n1,ProductionNodeKind::parallel,std::move(a),{after},{},true,false,{base.x+160,base.y-90}};nodes_[n2]={n2,ProductionNodeKind::parallel,std::move(b),{after},{},true,false,{base.x+160,base.y+90}};nodes_[mix]={mix,ProductionNodeKind::mixer,"Parallel Mixer",{n1,n2},{},true,false,{base.x+320,base.y}};for(auto&[nid,n]:nodes_)if(nid!=n1&&nid!=n2&&nid!=mix)for(auto&x:n.inputs)if(x==after)x=mix;selected_=n1;return{n1,n2};}
NodeId ProductionNodeGraph::convert_to_parallel(NodeId existing,std::string new_branch){auto current=node(existing);if(current.kind!=ProductionNodeKind::serial)throw std::invalid_argument("only serial node can convert to parallel");if(current.inputs.size()!=1)throw std::invalid_argument("serial node must have one input");const auto upstream=current.inputs.front();auto downstream=consumers(existing);nodes_.at(existing).kind=ProductionNodeKind::parallel;nodes_.at(existing).position.y-=90;NodeId branch=next_++,mix=next_++;auto base=nodes_.at(existing).position;nodes_[branch]={branch,ProductionNodeKind::parallel,std::move(new_branch),{upstream},{},true,false,{base.x,base.y+180}};nodes_[mix]={mix,ProductionNodeKind::mixer,"Parallel Mixer",{existing,branch},{},true,false,{base.x+160,base.y+90}};for(auto d:downstream){if(d==mix)continue;for(auto&i:nodes_.at(d).inputs)if(i==existing)i=mix;}selected_=branch;return branch;}
void ProductionNodeGraph::normalize_mixers(){bool changed=true;while(changed){changed=false;std::vector<NodeId> mids;for(auto&[id,n]:nodes_)if(n.kind==ProductionNodeKind::mixer)mids.push_back(id);for(auto mid:mids){if(!nodes_.contains(mid))continue;auto inputs=nodes_.at(mid).inputs;inputs.erase(std::remove_if(inputs.begin(),inputs.end(),[&](NodeId x){return !nodes_.contains(x);}),inputs.end());nodes_.at(mid).inputs=inputs;if(inputs.size()>=2){continue;}NodeId replacement=inputs.empty()?input_:inputs.front();for(auto&[nid,n]:nodes_)if(nid!=mid)for(auto&i:n.inputs)if(i==mid)i=replacement;nodes_.erase(mid);if(nodes_.contains(replacement)&&nodes_.at(replacement).kind==ProductionNodeKind::parallel)nodes_.at(replacement).kind=ProductionNodeKind::serial;changed=true;break;}}}
void ProductionNodeGraph::remove_node(NodeId id){auto n=node(id);if(n.kind==ProductionNodeKind::input||n.kind==ProductionNodeKind::output)throw std::invalid_argument("cannot remove endpoint");NodeId replacement=n.inputs.empty()?input_:n.inputs.front();NodeId owning_mixer{};NodeId surviving_branch{};for(auto&[nid,x]:nodes_)if(x.kind==ProductionNodeKind::mixer&&std::find(x.inputs.begin(),x.inputs.end(),id)!=x.inputs.end()){owning_mixer=nid;for(auto v:x.inputs)if(v!=id){surviving_branch=v;break;}}nodes_.erase(id);if(owning_mixer&&surviving_branch){for(auto&[nid,x]:nodes_)if(nid!=owning_mixer)for(auto&i:x.inputs)if(i==owning_mixer)i=surviving_branch;nodes_.erase(owning_mixer);nodes_.at(surviving_branch).kind=ProductionNodeKind::serial;replacement=surviving_branch;}else{for(auto&[_,x]:nodes_)for(auto&i:x.inputs)if(i==id)i=replacement;}if(selected_==id)selected_=(nodes_.contains(replacement)&&nodes_.at(replacement).kind!=ProductionNodeKind::input)?replacement:0;normalize_mixers();}
void ProductionNodeGraph::set_enabled(NodeId id,bool v){nodes_.at(id).enabled=v;}void ProductionNodeGraph::set_bypassed(NodeId id,bool v){nodes_.at(id).bypassed=v;}
void ProductionNodeGraph::add_operation_to_selected(NodeOperation op){if(!selected_)throw std::logic_error("no selected node");nodes_.at(selected_).operations.push_back(std::move(op));}
void ProductionNodeGraph::set_operation_on_selected(NodeOperation op){
 if(!selected_)throw std::logic_error("no selected node");
 auto& operations=nodes_.at(selected_).operations;
 auto it=std::find_if(operations.begin(),operations.end(),[&](const NodeOperation& current){return current.kind==op.kind;});
 if(it==operations.end())operations.push_back(std::move(op));else *it=std::move(op);
}
bool ProductionNodeGraph::remove_operation(NodeId id,NodeOperationKind kind){
 auto& operations=nodes_.at(id).operations;
 const auto old=operations.size();
 operations.erase(std::remove_if(operations.begin(),operations.end(),[&](const NodeOperation& op){return op.kind==kind;}),operations.end());
 return operations.size()!=old;
}
bool ProductionNodeGraph::set_operation_enabled(NodeId id,NodeOperationKind kind,bool enabled){
 auto& operations=nodes_.at(id).operations;
 auto it=std::find_if(operations.begin(),operations.end(),[&](const NodeOperation& op){return op.kind==kind;});
 if(it==operations.end())return false;
 if(it->enabled!=enabled)it->enabled=enabled;
 return true;
}
void ProductionNodeGraph::clear_operations(NodeId id){nodes_.at(id).operations.clear();}
std::vector<NodeId>ProductionNodeGraph::execution_order()const{std::vector<NodeId>out;std::unordered_set<NodeId>visiting,done;std::function<void(NodeId)>visit=[&](NodeId id){if(visiting.contains(id))throw std::logic_error("production node cycle");if(done.contains(id))return;visiting.insert(id);for(auto x:node(id).inputs)visit(x);visiting.erase(id);done.insert(id);out.push_back(id);};visit(output_);return out;}
NodeValue ProductionNodeGraph::mix_inputs(const std::vector<NodeValue>&v){if(v.empty())return{};NodeValue out=v.front();for(std::size_t p=0;p<out.size();++p){Color s{};for(auto&x:v){if(x.size()!=out.size())throw std::invalid_argument("mixer dimensions");s.r+=x[p].r;s.g+=x[p].g;s.b+=x[p].b;s.a+=x[p].a;}float n=1.f/v.size();out[p]={s.r*n,s.g*n,s.b*n,s.a*n};}return out;}
NodeValue ProductionNodeGraph::apply_operations(const ProductionNode& node,NodeValue value,std::uint32_t width,std::uint32_t height)const {
 const NodeValue original=value;
 std::vector<float> combined_mask(value.size(),1.0f);
 bool has_mask=false;

 for(const auto& operation:node.operations){
  if(!operation.enabled)continue;
  if(operation.kind==NodeOperationKind::hsl_qualifier){
   auto parameters=std::get<std::shared_ptr<const HslQualifierParameters>>(operation.payload);
   if(!parameters)throw std::invalid_argument("HSL qualifier parameters are missing");
   std::vector<float> matte(value.size());
   apply_hsl_qualifier_reference(original,matte,*parameters);
   for(std::size_t i=0;i<matte.size();++i)combined_mask[i]*=std::clamp(matte[i],0.0f,1.0f);
   has_mask=true;
  }else if(operation.kind==NodeOperationKind::power_window){
   const auto settings=std::get<PowerWindowSettings>(operation.payload);
   for(std::size_t i=0;i<combined_mask.size();++i){
    const float x=(float(i%width)+.5f)/float(width);
    const float y=(float(i/width)+.5f)/float(height);
    combined_mask[i]*=window_matte(settings,x,y);
   }
   has_mask=true;
  }
 }

 for(const auto& operation:node.operations){
  if(!operation.enabled||operation.kind==NodeOperationKind::hsl_qualifier||operation.kind==NodeOperationKind::power_window)continue;
  NodeValue before=value;
  if(operation.kind==NodeOperationKind::primary_wheels){
   auto p=std::get<std::shared_ptr<const PrimaryWheelsParameters>>(operation.payload);if(!p)throw std::invalid_argument("Primary Wheels parameters are missing");apply_primary_wheels_reference(before,value,*p);
  }else if(operation.kind==NodeOperationKind::log_wheels){
   auto p=std::get<std::shared_ptr<const LogWheelsParameters>>(operation.payload);if(!p)throw std::invalid_argument("Log Wheels parameters are missing");apply_log_wheels_reference(before,value,*p);
  }else if(operation.kind==NodeOperationKind::rgb_curves){
   auto p=std::get<std::shared_ptr<const CompiledRgbCurves>>(operation.payload);if(!p)throw std::invalid_argument("RGB Curves parameters are missing");p->apply(before,value);
  }else if(operation.kind==NodeOperationKind::correction){
   auto p=std::get<std::shared_ptr<const CorrectionParameters>>(operation.payload);if(!p)throw std::invalid_argument("Correction parameters are missing");apply_correction_reference(before,value,*p);
  }else if(operation.kind==NodeOperationKind::lut1d){
   auto p=std::get<std::shared_ptr<const Lut1D>>(operation.payload);if(!p)throw std::invalid_argument("1D LUT is missing");apply_lut_cpu(before.data(),value.data(),value.size(),*p);
  }else if(operation.kind==NodeOperationKind::lut3d){
   auto p=std::get<std::shared_ptr<const Lut3D>>(operation.payload);if(!p)throw std::invalid_argument("3D LUT is missing");apply_lut_cpu(before.data(),value.data(),value.size(),*p);
  }else if(operation.kind==NodeOperationKind::effect){
   CommandBuffer cb;CommandEncoder enc(cb);apply_effect_gpu(enc,before.data(),value.data(),width,height,std::get<EffectSettings>(operation.payload));enc.finish();CommandQueue queue;queue.submit(cb);
  }
 }

 if(has_mask){
  for(std::size_t i=0;i<value.size();++i)value[i]=blend(original[i],value[i],std::clamp(combined_mask[i],0.0f,1.0f));
 }
 return value;
}
NodeValue ProductionNodeGraph::render(std::span<const Color>source,std::uint32_t w,std::uint32_t h,std::int64_t)const{if(source.size()!=std::size_t(w)*h)throw std::invalid_argument("source dimensions");std::unordered_map<NodeId,NodeValue>values;for(auto id:execution_order()){auto&n=node(id);if(n.kind==ProductionNodeKind::input){values[id]=NodeValue(source.begin(),source.end());continue;}std::vector<NodeValue>in;for(auto x:n.inputs)in.push_back(values.at(x));NodeValue v=n.kind==ProductionNodeKind::mixer?mix_inputs(in):(in.empty()?NodeValue{}:in.front());if(n.enabled&&!n.bypassed&&n.kind!=ProductionNodeKind::output)v=apply_operations(n,std::move(v),w,h);values[id]=std::move(v);}return values.at(output_);}
void ProductionNodeGraph::clear_render_cache()const{render_cache_.clear();render_cache_bytes_=0;}
void ProductionNodeGraph::set_render_cache_budget_bytes(std::size_t bytes)const{render_cache_budget_bytes_=bytes;trim_render_cache(0,nullptr);}
void ProductionNodeGraph::trim_render_cache(NodeId protected_id,NodeRenderStats* stats)const{while(render_cache_bytes_>render_cache_budget_bytes_&&!render_cache_.empty()){auto victim=render_cache_.end();for(auto it=render_cache_.begin();it!=render_cache_.end();++it){if(it->first==protected_id)continue;if(victim==render_cache_.end()||it->second.last_use<victim->second.last_use)victim=it;}if(victim==render_cache_.end()) break;render_cache_bytes_-=victim->second.bytes;render_cache_.erase(victim);if(stats) ++stats->cache_evictions;}}
NodeValue ProductionNodeGraph::render_cached(std::span<const Color>source,std::uint32_t w,std::uint32_t h,std::uint64_t source_generation,NodeRenderStats* stats,std::int64_t frame)const{if(source.size()!=std::size_t(w)*h)throw std::invalid_argument("source dimensions");NodeRenderStats local{};std::unordered_map<NodeId,NodeValue>values;std::unordered_map<NodeId,std::string>keys;for(auto id:execution_order()){const auto&n=node(id);std::ostringstream key;if(n.kind==ProductionNodeKind::input){key<<"source:"<<source_generation<<':'<<w<<'x'<<h<<':'<<frame;}else{key<<id<<':'<<unsigned(n.kind)<<':'<<n.enabled<<':'<<n.bypassed;for(auto x:n.inputs)key<<"|in="<<keys.at(x);for(const auto&o:n.operations)key<<"|op="<<unsigned(o.kind)<<':'<<o.enabled<<':'<<o.identity;}auto key_string=key.str();auto cached=render_cache_.find(id);if(cached!=render_cache_.end()&&cached->second.key==key_string){cached->second.last_use=++render_cache_clock_;values[id]=cached->second.value;++local.cache_hits;keys[id]=std::move(key_string);continue;}++local.cache_misses;++local.executed_nodes;NodeValue v;if(n.kind==ProductionNodeKind::input)v.assign(source.begin(),source.end());else{std::vector<NodeValue>in;for(auto x:n.inputs)in.push_back(values.at(x));v=n.kind==ProductionNodeKind::mixer?mix_inputs(in):(in.empty()?NodeValue{}:in.front());if(n.enabled&&!n.bypassed&&n.kind!=ProductionNodeKind::output)v=apply_operations(n,std::move(v),w,h);}if(cached!=render_cache_.end()){render_cache_bytes_-=cached->second.bytes;render_cache_.erase(cached);}const auto entry_bytes=v.size()*sizeof(Color)+key_string.size();render_cache_[id]={key_string,v,++render_cache_clock_,entry_bytes};render_cache_bytes_+=entry_bytes;trim_render_cache(id,&local);values[id]=std::move(v);keys[id]=std::move(key_string);}local.cache_bytes=render_cache_bytes_;if(stats)*stats=local;return values.at(output_);}
std::string ProductionNodeGraph::recipe_identity()const{std::ostringstream o;for(auto id:execution_order()){auto&n=node(id);o<<id<<':'<<unsigned(n.kind)<<':'<<n.enabled<<':'<<n.bypassed<<':'<<n.position.x<<','<<n.position.y<<'[';for(auto x:n.inputs)o<<x<<',';o<<']';for(auto&op:n.operations)o<<'{'<<unsigned(op.kind)<<':'<<op.enabled<<':'<<op.identity<<'}';}return o.str();}
std::string ProductionNodeGraph::to_json()const{std::ostringstream o;o<<"{\"version\":2,\"input\":"<<input_<<",\"output\":"<<output_<<",\"selected\":"<<selected_<<",\"nodes\":[";bool first=true;std::vector<NodeId> ids;for(auto&[id,_]:nodes_)ids.push_back(id);std::sort(ids.begin(),ids.end());for(auto id:ids){auto&n=node(id);if(!first)o<<',';first=false;o<<"{\"id\":"<<id<<",\"kind\":"<<unsigned(n.kind)<<",\"name\":\""<<n.name<<"\",\"x\":"<<n.position.x<<",\"y\":"<<n.position.y<<",\"enabled\":"<<(n.enabled?"true":"false")<<",\"bypassed\":"<<(n.bypassed?"true":"false")<<",\"inputs\":[";for(size_t i=0;i<n.inputs.size();++i){if(i)o<<',';o<<n.inputs[i];}o<<"],\"operations\":[";for(size_t i=0;i<n.operations.size();++i){if(i)o<<',';auto&op=n.operations[i];o<<"{\"kind\":"<<unsigned(op.kind)<<",\"enabled\":"<<(op.enabled?"true":"false")<<",\"identity\":\""<<op.identity<<"\"}";}o<<"]}";}o<<"]}";return o.str();}
void ProductionNodeGraph::serialize(std::ostream&o)const{o<<to_json()<<'\n';}
NodeOperation make_primary_wheels_operation(std::shared_ptr<const PrimaryWheelsParameters>p){if(!p)throw std::invalid_argument("primary");return{NodeOperationKind::primary_wheels,p,true,opid(NodeOperationKind::primary_wheels,p->identity())};}
NodeOperation make_log_wheels_operation(std::shared_ptr<const LogWheelsParameters>p){if(!p)throw std::invalid_argument("log");return{NodeOperationKind::log_wheels,p,true,opid(NodeOperationKind::log_wheels,p->identity())};}
NodeOperation make_rgb_curves_operation(std::shared_ptr<const CompiledRgbCurves>p){if(!p)throw std::invalid_argument("curves");return{NodeOperationKind::rgb_curves,p,true,opid(NodeOperationKind::rgb_curves,p->identity())};}
NodeOperation make_hsl_qualifier_operation(std::shared_ptr<const HslQualifierParameters>p){if(!p)throw std::invalid_argument("qualifier");return{NodeOperationKind::hsl_qualifier,p,true,opid(NodeOperationKind::hsl_qualifier,p->identity())};}
NodeOperation make_correction_operation(std::shared_ptr<const CorrectionParameters>p){if(!p)throw std::invalid_argument("correction");return{NodeOperationKind::correction,p,true,opid(NodeOperationKind::correction,std::string(p->identity()))};}
NodeOperation make_lut_operation(std::shared_ptr<const Lut1D>p,LutInterpolation){if(!p)throw std::invalid_argument("lut1d");return{NodeOperationKind::lut1d,p,true,"lut1d:"+std::to_string(p->values().size())};}
NodeOperation make_lut_operation(std::shared_ptr<const Lut3D>p,LutInterpolation){if(!p)throw std::invalid_argument("lut3d");return{NodeOperationKind::lut3d,p,true,"lut3d:"+std::to_string(p->size())};}
NodeOperation make_effect_operation(EffectSettings s){return{NodeOperationKind::effect,s,true,"effect:"+std::to_string(unsigned(s.type))+":"+std::to_string(s.amount)+":"+std::to_string(s.radius)};}
NodeOperation make_power_window_operation(PowerWindowSettings s){return{NodeOperationKind::power_window,s,true,"window:"+std::to_string(unsigned(s.shape))+":"+std::to_string(s.center_x)+":"+std::to_string(s.center_y)};}
}
