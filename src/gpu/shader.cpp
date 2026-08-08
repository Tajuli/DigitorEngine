#include "digitor/shader.hpp"
#include "core/environment.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace digitor {
namespace {
std::string hash_bytes(std::span<const std::byte> bytes) {
    std::uint64_t a=1469598103934665603ull,b=1099511628211ull;
    for(auto v:bytes){a=(a^std::to_integer<unsigned char>(v))*1099511628211ull;b=(b+std::to_integer<unsigned char>(v))*0x9e3779b185ebca87ull;}
    std::ostringstream o;o<<std::hex<<std::setfill('0')<<std::setw(16)<<a<<std::setw(16)<<b;return o.str();
}
std::string hash_string(std::string_view s){return hash_bytes({reinterpret_cast<const std::byte*>(s.data()),s.size()});}
std::string quote(const std::filesystem::path&p){std::string s=p.string(),o="\"";for(char c:s){if(c=='\"')o+='\\';o+=c;}return o+'\"';}
int run_shell_command(const std::string& command){
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)command;
    return -1;
#elif defined(_WIN32)
    const std::string wrapped = "\"" + command + "\"";
    return std::system(wrapped.c_str());
#else
    return std::system(command.c_str());
#endif
}
std::string read_text(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::vector<std::byte> read_binary(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::vector<char> c{std::istreambuf_iterator<char>(f),{}};std::vector<std::byte>b(c.size());std::transform(c.begin(),c.end(),b.begin(),[](char x){return std::byte(static_cast<unsigned char>(x));});return b;}
std::string executable_version(const std::filesystem::path&p){if(p.empty())return {};auto tmp=std::filesystem::temp_directory_path()/("digitor-version-"+hash_string(p.string())+".txt");std::string command=quote(p)+" --version > "+quote(tmp)+" 2>&1";if(run_shell_command(command)!=0)return {};auto v=read_text(tmp);std::error_code ec;std::filesystem::remove(tmp,ec);while(!v.empty()&&(v.back()=='\n'||v.back()=='\r'))v.pop_back();return v;}
bool below(const std::filesystem::path&p,const std::filesystem::path&root){auto pi=p.begin(),ri=root.begin();for(;ri!=root.end();++ri,++pi)if(pi==p.end()||*pi!=*ri)return false;return true;}
struct Includes { std::vector<ShaderDependency> deps; std::vector<std::filesystem::path> stack; std::string error; };
bool scan_includes(std::string_view source,const std::vector<std::filesystem::path>&roots,Includes&out){
 std::istringstream lines{std::string(source)};std::string line;
 while(std::getline(lines,line)){auto p=line.find_first_not_of(" \t");if(p==std::string::npos||line.compare(p,8,"#include"))continue;p=line.find_first_of("\"<",p+8);if(p==std::string::npos){out.error="malformed #include";return false;}char close=line[p]=='\"'?'\"':'>';auto e=line.find(close,p+1);if(e==std::string::npos){out.error="malformed #include";return false;}std::filesystem::path rel=line.substr(p+1,e-p-1);if(rel.is_absolute()){out.error="absolute include is forbidden: "+rel.string();return false;}std::filesystem::path found;for(auto root:roots){std::error_code ec;root=std::filesystem::weakly_canonical(root,ec);auto candidate=std::filesystem::weakly_canonical(root/rel,ec);if(!ec&&below(candidate,root)&&std::filesystem::is_regular_file(candidate)){found=candidate;break;}}if(found.empty()){out.error="include not found within configured roots: "+rel.generic_string();return false;}if(std::find(out.stack.begin(),out.stack.end(),found)!=out.stack.end()){out.error="include cycle at "+found.generic_string();return false;}auto text=read_text(found);out.deps.push_back({found.generic_string(),hash_string(text)});out.stack.push_back(found);if(!scan_includes(text,roots,out))return false;out.stack.pop_back();}
 return true;
}
std::string stage_profile(ShaderStage s){switch(s){case ShaderStage::vertex:return "vs_6_0";case ShaderStage::fragment:return "ps_6_0";case ShaderStage::compute:return "cs_6_0";}return {};}
std::string make_key(const ShaderCompileRequest&r,std::string_view version,const std::vector<ShaderDependency>&d){std::ostringstream o;o<<"ABI="<<shader_abi_version<<"\n"<<r.source<<"\nENTRY="<<r.entry_point<<"\nSTAGE="<<int(r.stage)<<"\nBACKEND="<<int(r.backend)<<"\nPROFILE="<<r.target_profile<<"\nOPT="<<int(r.optimization)<<"\nDEBUG="<<r.debug_info<<"\nCOMPILER="<<version;auto m=r.macros;std::sort(m.begin(),m.end(),[](auto&a,auto&b){return std::tie(a.name,a.value)<std::tie(b.name,b.value);});for(auto&x:m)o<<"\nD="<<x.name<<'='<<x.value;auto deps=d;std::sort(deps.begin(),deps.end(),[](auto&a,auto&b){return a.normalized_path<b.normalized_path;});for(auto&x:deps)o<<"\nI="<<x.normalized_path<<'='<<x.content_hash;for(auto&s:r.specialization_constants)o<<"\nS="<<s.id<<'='<<hash_bytes(s.value);return hash_string(o.str());}
std::uint32_t word(const std::vector<std::byte>&b,std::size_t n){std::uint32_t v;std::memcpy(&v,b.data()+n*4,4);return v;}
std::string spv_string(const std::vector<std::byte>&b,std::size_t begin,std::size_t end){std::string s;for(auto i=begin;i<end;i++){auto w=word(b,i);for(unsigned j=0;j<4;j++){char c=char(w>>(8*j));if(!c)return s;s+=c;}}return s;}
bool reflect_spirv(const std::vector<std::byte>&b,ShaderStage requested,ShaderReflection&out,std::string&diag){
 if(b.size()<20||b.size()%4||word(b,0)!=0x07230203u){diag="invalid SPIR-V header";return false;}struct Deco{std::optional<std::uint32_t>set,binding,location,spec;};std::unordered_map<std::uint32_t,Deco>d;std::unordered_map<std::uint32_t,std::string>names;struct Var{std::uint32_t type,storage;};std::unordered_map<std::uint32_t,Var>vars;std::unordered_map<std::uint32_t,std::uint32_t>pointer_storage;
 bool entry=false;for(std::size_t i=5;i<b.size()/4;){auto first=word(b,i),wc=first>>16,op=first&0xffff;if(!wc||i+wc>b.size()/4){diag="malformed SPIR-V instruction";return false;}if(op==5&&wc>=3)names[word(b,i+1)]=spv_string(b,i+2,i+wc);else if(op==15&&wc>=4){auto model=word(b,i+1);ShaderStage st=model==0?ShaderStage::vertex:model==4?ShaderStage::fragment:ShaderStage::compute;if(st==requested){out.entry_point=spv_string(b,i+3,i+wc);entry=true;}}else if(op==16&&wc>=6&&word(b,i+2)==17)out.workgroup_size={word(b,i+3),word(b,i+4),word(b,i+5)};else if(op==71&&wc>=3){auto id=word(b,i+1),kind=word(b,i+2);if(kind==33&&wc>=4)d[id].binding=word(b,i+3);else if(kind==34&&wc>=4)d[id].set=word(b,i+3);else if(kind==30&&wc>=4)d[id].location=word(b,i+3);else if(kind==1&&wc>=4)d[id].spec=word(b,i+3);}else if(op==32&&wc>=4)pointer_storage[word(b,i+1)]=word(b,i+2);else if(op==59&&wc>=4)vars[word(b,i+2)]={word(b,i+1),word(b,i+3)};i+=wc;}
 if(!entry){diag="compiled binary does not contain the requested entry point/stage";return false;}out.stage=requested;for(auto&[id,v]:vars){auto it=d.find(id);if(it!=d.end()&&it->second.binding){ShaderBinding x;x.binding=*it->second.binding;x.set=it->second.set.value_or(0);x.name=names[id];x.type=v.storage==2?ShaderResourceType::uniform_buffer:v.storage==12?ShaderResourceType::storage_buffer:ShaderResourceType::sampled_image;x.read_only=v.storage!=12;out.bindings.push_back(std::move(x));}if(it!=d.end()&&it->second.location){ShaderInterfaceVariable x{*it->second.location,names[id]};if(requested==ShaderStage::vertex&&v.storage==1)out.vertex_inputs.push_back(x);if(requested==ShaderStage::fragment&&v.storage==3)out.fragment_outputs.push_back(x);}}for(auto&[id,x]:d)if(x.spec)out.specialization_constants.push_back(*x.spec);return true;
}
std::string pipeline_key(const PipelineDescription&d){std::ostringstream o;o<<d.reflection_layout_key<<'|'<<d.render_target_formats<<'|'<<d.depth_stencil_format<<'|'<<d.blend_state<<'|'<<d.raster_state<<'|'<<d.sample_count<<'|'<<d.topology<<'|'<<d.vertex_layout<<'|'<<d.device_compatibility<<'|'<<d.abi_version;for(auto&s:d.shader_binary_keys)o<<'|'<<s;for(auto&s:d.specialization_constants)o<<'|'<<s.id<<':'<<hash_bytes(s.value);return hash_string(o.str());}
}
ShaderCompiler::ShaderCompiler(std::filesystem::path dxc,std::filesystem::path spirv):dxc_(std::move(dxc)),spirv_val_(std::move(spirv)){if(dxc_.empty()){if(auto p=environment_variable("DIGITOR_DXC"))dxc_=*p;
#ifdef DIGITOR_CONFIGURED_DXC
else dxc_=DIGITOR_CONFIGURED_DXC;
#endif
}if(spirv_val_.empty()){if(auto p=environment_variable("DIGITOR_SPIRV_VAL"))spirv_val_=*p;
#ifdef DIGITOR_CONFIGURED_SPIRV_VAL
else spirv_val_=DIGITOR_CONFIGURED_SPIRV_VAL;
#endif
}}
std::string ShaderCompiler::identity()const{return dxc_.empty()?"DXC unavailable":"DXC: "+executable_version(dxc_);}
std::string ShaderCompiler::cache_key(const ShaderCompileRequest&r)const noexcept{try{Includes inc;if(r.source.empty()||!scan_includes(r.source,r.include_roots,inc))return {};return make_key(r,executable_version(dxc_),inc.deps);}catch(...){return {};}}
ShaderCompileResult ShaderCompiler::compile(const ShaderCompileRequest&r)const noexcept{ShaderCompileResult z;z.reflection.stage=r.stage;z.reflection.entry_point=r.entry_point;try{if(r.source.empty()||r.entry_point.empty()){z.error=ShaderError::invalid_argument;z.diagnostics="source and entry point are required";return z;}if(r.backend==ShaderBackend::metal||r.backend==ShaderBackend::opengles){z.error=ShaderError::compiler_unavailable;z.diagnostics="this build has no configured SPIRV-Cross/native offline compiler for the requested target";return z;}if(dxc_.empty()||!std::filesystem::exists(dxc_)){z.error=ShaderError::compiler_unavailable;z.diagnostics="DXC was not configured; set DIGITOR_DXC or CMake DIGITOR_DXC_EXECUTABLE";return z;}Includes inc;if(!scan_includes(r.source,r.include_roots,inc)){z.error=ShaderError::include_error;z.diagnostics=inc.error;return z;}z.dependencies=inc.deps;z.compiler_identity="DXC";z.compiler_version=executable_version(dxc_);z.target_profile=r.target_profile.empty()?stage_profile(r.stage):r.target_profile;z.cache_key=make_key(r,z.compiler_version,z.dependencies);auto persistent_root=environment_variable("DIGITOR_SHADER_CACHE_DIR").value_or((std::filesystem::temp_directory_path()/"digitor-shader-cache").string());std::filesystem::create_directories(persistent_root);auto persistent=std::filesystem::path(persistent_root)/(z.cache_key+(r.backend==ShaderBackend::vulkan?".spv":".dxil"));if(std::filesystem::exists(persistent)){z.binary=read_binary(persistent);z.format=r.backend==ShaderBackend::vulkan?ShaderBinaryFormat::spirv:ShaderBinaryFormat::dxil;if(r.backend!=ShaderBackend::vulkan||reflect_spirv(z.binary,r.stage,z.reflection,z.diagnostics)){z.error=ShaderError::none;z.diagnostics="persistent shader cache hit";return z;}z.binary.clear();}auto dir=std::filesystem::temp_directory_path()/("digitor-shader-"+z.cache_key);std::filesystem::create_directories(dir);auto input=dir/std::filesystem::path(r.source_name).filename(),output=dir/(r.backend==ShaderBackend::vulkan?"shader.spv":"shader.dxil"),log=dir/"diagnostics.txt";{std::ofstream f(input,std::ios::binary);f<<r.source;}std::ostringstream cmd;cmd<<quote(dxc_)<<' '<<quote(input)<<" -E "<<r.entry_point<<" -T "<<z.target_profile<<" -Fo "<<quote(output);if(r.backend==ShaderBackend::vulkan)cmd<<" -spirv -fspv-target-env=vulkan1.0 -fvk-use-dx-layout";cmd<<(r.optimization==ShaderOptimization::none?" -Od":r.optimization==ShaderOptimization::size?" -O1":" -O3");if(r.debug_info)cmd<<" -Zi -Qembed_debug";for(auto&m:r.macros)cmd<<" -D"<<m.name<<'='<<m.value;for(auto&p:r.include_roots)cmd<<" -I "<<quote(std::filesystem::weakly_canonical(p));cmd<<" > "<<quote(log)<<" 2>&1";int rc=run_shell_command(cmd.str());z.diagnostics=read_text(log);if(rc||!std::filesystem::exists(output)){z.error=z.diagnostics.find("entry point")!=std::string::npos?ShaderError::missing_entry_point:ShaderError::compile_failure;std::filesystem::remove_all(dir);return z;}z.binary=read_binary(output);if(z.binary.empty()){z.error=ShaderError::compile_failure;z.diagnostics+="\ncompiler returned an empty binary";std::filesystem::remove_all(dir);return z;}if(r.backend==ShaderBackend::vulkan){z.format=ShaderBinaryFormat::spirv;if(spirv_val_.empty()||!std::filesystem::exists(spirv_val_)){z.error=ShaderError::compiler_unavailable;z.diagnostics+="\nSPIR-V validation requires spirv-val";std::filesystem::remove_all(dir);return z;}auto vlog=dir/"validation.txt";auto vc=quote(spirv_val_)+" --target-env vulkan1.0 "+quote(output)+" > "+quote(vlog)+" 2>&1";if(run_shell_command(vc)){z.error=ShaderError::compile_failure;z.diagnostics+=read_text(vlog);std::filesystem::remove_all(dir);return z;}if(!reflect_spirv(z.binary,r.stage,z.reflection,z.diagnostics)){z.error=ShaderError::reflection_failure;std::filesystem::remove_all(dir);return z;}}else{z.format=ShaderBinaryFormat::dxil;z.reflection.stage=r.stage;z.reflection.entry_point=r.entry_point;}z.error=ShaderError::none;{std::ofstream cached(persistent,std::ios::binary|std::ios::trunc);cached.write(reinterpret_cast<const char*>(z.binary.data()),static_cast<std::streamsize>(z.binary.size()));}std::filesystem::remove_all(dir);return z;}catch(const std::bad_alloc&){z.error=ShaderError::out_of_memory;z.diagnostics="out of memory";}catch(const std::exception&e){z.error=ShaderError::compile_failure;z.diagnostics=e.what();}return z;}
ShaderCompileResult ShaderCache::get_or_compile(const ShaderCompiler&c,const ShaderCompileRequest&r){const auto key=c.cache_key(r);std::scoped_lock l(mutex_);if(!key.empty())if(auto i=entries_.find(key);i!=entries_.end())return *i->second;auto ptr=std::make_shared<ShaderCompileResult>(c.compile(r));if(!ptr->cache_key.empty())entries_[ptr->cache_key]=ptr;return *ptr;}
std::size_t ShaderCache::size()const{std::scoped_lock l(mutex_);return entries_.size();}
std::shared_ptr<NativePipeline> PipelineCache::get_or_create(const PipelineDescription&d,const Factory&f){if(!f||d.shader_binary_keys.empty()||d.device_compatibility.empty())throw std::invalid_argument("pipeline requires shaders, device identity, and native factory");auto key=pipeline_key(d);std::scoped_lock l(mutex_);if(auto i=entries_.find(key);i!=entries_.end())return i->second;auto p=f();if(!p)throw std::runtime_error("native pipeline creation returned null");entries_[key]=p;return p;}
std::size_t PipelineCache::size()const{std::scoped_lock l(mutex_);return entries_.size();}
ShaderError validate_layout(const ShaderReflection&r,std::span<const CpuBufferLayout>cpu,std::string&diag)noexcept{try{for(auto&b:r.bindings)if(b.type==ShaderResourceType::uniform_buffer||b.type==ShaderResourceType::storage_buffer){auto i=std::find_if(cpu.begin(),cpu.end(),[&](auto&x){return x.set==b.set&&x.binding==b.binding;});if(i==cpu.end()){diag="missing CPU binding";return ShaderError::layout_mismatch;}if(b.buffer_size&&i->size!=b.buffer_size){diag="buffer size mismatch";return ShaderError::layout_mismatch;}for(auto&f:b.fields){auto j=std::find_if(i->fields.begin(),i->fields.end(),[&](auto&x){return x.name==f.name;});if(j==i->fields.end()||j->offset!=f.offset||j->size!=f.size||j->matrix_layout!=f.matrix_layout){diag="field layout mismatch: "+f.name;return ShaderError::layout_mismatch;}}}diag.clear();return ShaderError::none;}catch(...){diag="layout validation failed";return ShaderError::layout_mismatch;}}
void CpuKernelRegistry::register_kernel(std::string id,Kernel k){if(id.empty()||!k)throw std::invalid_argument("CPU kernel requires operation ID and function");std::scoped_lock l(mutex_);if(!kernels_.emplace(std::move(id),std::move(k)).second)throw std::invalid_argument("duplicate CPU operation ID");}
bool CpuKernelRegistry::execute(std::string_view id,std::span<const std::byte>in,std::span<std::byte>out)const{Kernel k;{std::scoped_lock l(mutex_);auto i=kernels_.find(std::string(id));if(i==kernels_.end())return false;k=i->second;}k(in,out);return true;}
} // namespace digitor