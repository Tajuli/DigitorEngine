#include <digitor/digitor.h>
#include <stddef.h>

int main(void) {
    _Static_assert(offsetof(DigitorTextureDesc, usage) == 12, "texture ABI changed");
    _Static_assert(sizeof(DigitorBufferDesc) >= 12, "buffer ABI changed");
    DigitorTextureDesc texture = {
        1u, 1u, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT, DIGITOR_TEXTURE_USAGE_SAMPLED
    };
    DigitorBufferDesc buffer = {16u, DIGITOR_BUFFER_USAGE_UNIFORM};
    return (texture.width == 1u && buffer.size == 16u) ? 0 : 1;
}
