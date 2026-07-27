#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

inline constexpr std::uint32_t shader_abi_version = 1;

enum class ShaderStage { vertex, fragment, compute };
enum class ShaderBackend { vulkan, d3d12, metal, opengles };
enum class ShaderBinaryFormat { spirv, dxil, metallib, glsl_es };
enum class ShaderOptimization { none, size, performance };
enum class ShaderError {
    none, compiler_unavailable, compile_failure, invalid_source, missing_entry_point,
    include_error, reflection_failure, layout_mismatch, unsupported_shader_stage,
    unsupported_target_profile, native_module_creation_failure,
    native_pipeline_creation_failure, cache_corruption, cache_io_failure,
    out_of_memory, device_lost, invalid_argument
};
enum class ShaderResourceType {
    uniform_buffer, storage_buffer, sampled_image, storage_image, sampler,
    combined_image_sampler, push_constant
};
enum class MatrixLayout { not_applicable, row_major, column_major };

struct ShaderMacro { std::string name, value; };
struct SpecializationConstant { std::uint32_t id{}; std::vector<std::byte> value; };
struct ShaderDependency { std::string normalized_path, content_hash; };
struct ShaderField {
    std::string name;
    std::uint32_t offset{}, size{};
    MatrixLayout matrix_layout{MatrixLayout::not_applicable};
};
struct ShaderBinding {
    std::uint32_t set{}, binding{}, array_count{1}, buffer_size{};
    std::string name;
    ShaderResourceType type{ShaderResourceType::uniform_buffer};
    bool read_only{true};
    std::vector<ShaderField> fields;
};
struct ShaderInterfaceVariable {
    std::uint32_t location{};
    std::string name;
};
struct ShaderReflection {
    ShaderStage stage{ShaderStage::compute};
    std::string entry_point;
    std::vector<ShaderBinding> bindings;
    std::vector<ShaderInterfaceVariable> vertex_inputs, fragment_outputs;
    std::vector<std::uint32_t> specialization_constants;
    std::array<std::uint32_t, 3> workgroup_size{1, 1, 1};
};
struct ShaderCompileRequest {
    std::string source, entry_point{"main"}, source_name{"shader.hlsl"}, target_profile;
    ShaderStage stage{ShaderStage::compute};
    ShaderBackend backend{ShaderBackend::vulkan};
    std::vector<ShaderMacro> macros;
    std::vector<std::filesystem::path> include_roots;
    std::vector<SpecializationConstant> specialization_constants;
    ShaderOptimization optimization{ShaderOptimization::performance};
    bool debug_info{};
};
struct ShaderCompileResult {
    ShaderError error{ShaderError::invalid_argument};
    ShaderBinaryFormat format{ShaderBinaryFormat::spirv};
    std::vector<std::byte> binary;
    std::string diagnostics, cache_key, compiler_identity, compiler_version, target_profile;
    std::vector<ShaderDependency> dependencies;
    ShaderReflection reflection;
    explicit operator bool() const noexcept { return error == ShaderError::none && !binary.empty(); }
};

class ShaderCompiler {
public:
    ShaderCompiler(std::filesystem::path dxc = {}, std::filesystem::path spirv_val = {});
    ShaderCompileResult compile(const ShaderCompileRequest&) const noexcept;
    std::string cache_key(const ShaderCompileRequest&) const noexcept;
    std::string identity() const;
private:
    std::filesystem::path dxc_, spirv_val_;
};

class ShaderCache {
public:
    ShaderCache() = default;
    ShaderCompileResult get_or_compile(const ShaderCompiler&, const ShaderCompileRequest&);
    std::size_t size() const;
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ShaderCompileResult>> entries_;
};

struct PipelineDescription {
    std::vector<std::string> shader_binary_keys;
    std::string reflection_layout_key, render_target_formats, depth_stencil_format;
    std::string blend_state, raster_state, topology, vertex_layout, device_compatibility;
    std::uint32_t sample_count{1}, abi_version{shader_abi_version};
    std::vector<SpecializationConstant> specialization_constants;
};
struct NativePipeline {
    virtual ~NativePipeline() = default;
    virtual ShaderBackend backend() const noexcept = 0;
};
class PipelineCache {
public:
    using Factory = std::function<std::shared_ptr<NativePipeline>()>;
    std::shared_ptr<NativePipeline> get_or_create(const PipelineDescription&, const Factory&);
    std::size_t size() const;
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<NativePipeline>> entries_;
};

struct CpuLayoutField { std::string name; std::uint32_t offset{}, size{}; MatrixLayout matrix_layout{}; };
struct CpuBufferLayout { std::uint32_t set{}, binding{}, size{}; std::vector<CpuLayoutField> fields; };
ShaderError validate_layout(const ShaderReflection&, std::span<const CpuBufferLayout>, std::string& diagnostics) noexcept;

class CpuKernelRegistry {
public:
    using Kernel = std::function<void(std::span<const std::byte>, std::span<std::byte>)>;
    void register_kernel(std::string operation_id, Kernel);
    bool execute(std::string_view operation_id, std::span<const std::byte>, std::span<std::byte>) const;
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Kernel> kernels_;
};

} // namespace digitor
