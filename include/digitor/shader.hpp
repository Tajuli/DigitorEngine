#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace digitor {
enum class ShaderLanguage { glsl, spirv, msl, hlsl };
enum class ShaderStage { vertex, fragment, compute };
struct ShaderBinding { uint32_t set{}, binding{}; std::string name; };
struct ShaderReflection { ShaderStage stage{ShaderStage::compute}; std::vector<ShaderBinding> bindings; uint32_t workgroup_size[3]{1,1,1}; };
struct CompiledShader { ShaderLanguage language; std::vector<uint32_t> spirv; std::string source; ShaderReflection reflection; uint64_t hash{}; };
class ShaderCompiler { public: CompiledShader compile(ShaderLanguage, ShaderStage, const std::string&); };
class ShaderCache { public: const CompiledShader& get_or_compile(ShaderCompiler&,ShaderLanguage,ShaderStage,const std::string&); size_t size()const{return entries_.size();} private:std::unordered_map<std::string,CompiledShader>entries_;};
class PipelineCache { public: uint64_t get_or_create(const std::vector<uint64_t>&); size_t size()const{return entries_.size();} private:std::unordered_map<std::string,uint64_t>entries_;};
class DescriptorCache { public: uint64_t get_or_create(std::string_view); size_t size()const{return entries_.size();} private:std::unordered_map<std::string,uint64_t>entries_;};
class SamplerCache { public: uint64_t get_or_create(std::string_view); size_t size()const{return entries_.size();} private:std::unordered_map<std::string,uint64_t>entries_;};
struct RenderPipelineCaches { ShaderCache shaders; PipelineCache pipelines; DescriptorCache descriptors; SamplerCache samplers; };
}
