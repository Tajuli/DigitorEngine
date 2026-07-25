#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "digitor/digitor.h"

namespace digitor {

class RenderContext;

class Texture final {
public:
    Texture(RenderContext& owner, const DigitorTextureDesc& desc, std::size_t byte_size);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

private:
    RenderContext& owner_;
    DigitorTextureDesc desc_;
    std::vector<std::byte> storage_;
};

class Buffer final {
public:
    Buffer(RenderContext& owner, const DigitorBufferDesc& desc);
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

private:
    RenderContext& owner_;
    DigitorBufferDesc desc_;
    std::vector<std::byte> storage_;
};

}  // namespace digitor
