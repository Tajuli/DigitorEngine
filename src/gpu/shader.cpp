#include "digitor/shader.hpp"

#include <limits>
#include <regex>
#include <stdexcept>

namespace digitor {
namespace {
uint64_t stable_hash(const std::string& source) {
    uint64_t hash = 1469598103934665603ull;
    for (char value : source) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint32_t parse_u32(const std::string& text) {
    const unsigned long value = std::stoul(text);
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw std::out_of_range("shader integer exceeds uint32 range");
    }
    return static_cast<uint32_t>(value);
}
}  // namespace

CompiledShader ShaderCompiler::compile(ShaderLanguage language, ShaderStage stage,
                                       const std::string& source) {
    if (source.empty()) throw std::invalid_argument("empty shader");
    CompiledShader out{language, {}, source, {stage, {}, {1, 1, 1}},
                       stable_hash(std::to_string(static_cast<int>(language)) + source)};
    if (language == ShaderLanguage::spirv) {
        if (source.size() % 4 != 0) throw std::invalid_argument("SPIR-V byte size");
        for (std::size_t index = 0; index < source.size(); index += 4) {
            const auto byte = [&](std::size_t offset) {
                return static_cast<uint32_t>(static_cast<unsigned char>(source[index + offset]));
            };
            out.spirv.push_back(byte(0) | (byte(1) << 8u) | (byte(2) << 16u) | (byte(3) << 24u));
        }
        if (out.spirv.empty() || out.spirv[0] != 0x07230203u) {
            throw std::invalid_argument("SPIR-V magic");
        }
    } else {
        if (source.find("main") == std::string::npos) {
            throw std::invalid_argument("shader has no entry point");
        }
        const std::regex binding(R"((?:binding\s*=\s*|register\s*\(\s*[tubs])(\d+))");
        for (std::sregex_iterator iterator(source.begin(), source.end(), binding), end;
             iterator != end; ++iterator) {
            out.reflection.bindings.push_back({0, parse_u32((*iterator)[1].str()), {}});
        }
        const std::regex local(R"(local_size_x\s*=\s*(\d+))");
        std::smatch match;
        if (std::regex_search(source, match, local)) {
            out.reflection.workgroup_size[0] = parse_u32(match[1].str());
        }
    }
    return out;
}

const CompiledShader& ShaderCache::get_or_compile(ShaderCompiler& compiler, ShaderLanguage language,
                                                   ShaderStage stage, const std::string& source) {
    const std::string key = std::to_string(static_cast<int>(language)) + ':' +
                            std::to_string(static_cast<int>(stage)) + ':' + source;
    auto iterator = entries_.find(key);
    if (iterator == entries_.end()) {
        iterator = entries_.emplace(key, compiler.compile(language, stage, source)).first;
    }
    return iterator->second;
}

uint64_t PipelineCache::get_or_create(const std::vector<uint64_t>& shaders) {
    std::string key;
    for (uint64_t shader : shaders) key += std::to_string(shader) + ':';
    const auto iterator = entries_.find(key);
    if (iterator != entries_.end()) return iterator->second;
    const uint64_t identifier = stable_hash(key);
    entries_[key] = identifier;
    return identifier;
}

uint64_t DescriptorCache::get_or_create(std::string_view layout) {
    if (layout.empty()) throw std::invalid_argument("empty descriptor layout");
    const std::string key(layout);
    const auto [iterator, inserted] = entries_.emplace(key, 0);
    if (inserted) iterator->second = stable_hash("descriptor:" + key);
    return iterator->second;
}

uint64_t SamplerCache::get_or_create(std::string_view description) {
    if (description.empty()) throw std::invalid_argument("empty sampler description");
    const std::string key(description);
    const auto [iterator, inserted] = entries_.emplace(key, 0);
    if (inserted) iterator->second = stable_hash("sampler:" + key);
    return iterator->second;
}
}  // namespace digitor
