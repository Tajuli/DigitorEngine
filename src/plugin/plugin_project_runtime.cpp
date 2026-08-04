#include "digitor/plugin_project_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace digitor {
namespace { bool hex64(const std::string&s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});} }
bool PluginProjectRuntime::load_snapshot(const std::string& text,std::string& diagnostic){
  std::vector<PluginProjectInstance> next; std::istringstream in(text); std::string line;
  while(std::getline(in,line)){if(line.empty())continue;std::istringstream row(line);PluginProjectInstance x;unsigned kind=0,enabled=0;
    if(!(row>>x.instance_id>>x.plugin_id>>x.version>>x.package_sha256>>kind>>enabled)||x.instance_id.empty()||x.plugin_id.empty()||x.version.empty()||!hex64(x.package_sha256)||kind>2){diagnostic="invalid project plugin snapshot";return false;}
    x.kind=static_cast<PluginProjectKind>(kind);x.enabled=enabled!=0;
    if(std::any_of(next.begin(),next.end(),[&](const auto&v){return v.instance_id==x.instance_id;})){diagnostic="duplicate plugin instance";return false;} next.push_back(std::move(x));}
  instances_=std::move(next);diagnostic.clear();return true;
}
std::string PluginProjectRuntime::save_snapshot()const{auto sorted=instances_;std::sort(sorted.begin(),sorted.end(),[](const auto&a,const auto&b){return a.instance_id<b.instance_id;});std::ostringstream out;for(const auto&x:sorted)out<<x.instance_id<<' '<<x.plugin_id<<' '<<x.version<<' '<<x.package_sha256<<' '<<static_cast<unsigned>(x.kind)<<' '<<(x.enabled?1:0)<<'\n';return out.str();}
bool PluginProjectRuntime::activate(const std::string&id,const std::vector<PluginInstalledIdentity>&installed,PluginProjectResolution&out){auto it=std::find_if(instances_.begin(),instances_.end(),[&](const auto&x){return x.instance_id==id;});if(it==instances_.end()){out={PluginProjectState::missing,"instance not found"};return false;}auto p=std::find_if(installed.begin(),installed.end(),[&](const auto&x){return x.plugin_id==it->plugin_id&&x.version==it->version&&x.package_sha256==it->package_sha256;});if(p==installed.end()){it->enabled=false;out={PluginProjectState::missing,"exact pinned package is not installed"};return false;}if(p->revoked){it->enabled=false;out={PluginProjectState::revoked,"pinned package is revoked"};return false;}if(!p->compatible){it->enabled=false;out={PluginProjectState::incompatible,"pinned package is incompatible"};return false;}it->enabled=true;out={PluginProjectState::ready,{}};return true;}
bool PluginProjectRuntime::deactivate(const std::string&id){auto it=std::find_if(instances_.begin(),instances_.end(),[&](const auto&x){return x.instance_id==id;});if(it==instances_.end())return false;it->enabled=false;return true;}
bool PluginProjectRuntime::set_numeric_parameter(const std::string&id,const std::string&parameter,double value){if(parameter.empty()||!std::isfinite(value))return false;auto it=std::find_if(instances_.begin(),instances_.end(),[&](const auto&x){return x.instance_id==id;});if(it==instances_.end())return false;it->numeric_parameters[parameter]=value;return true;}
}
