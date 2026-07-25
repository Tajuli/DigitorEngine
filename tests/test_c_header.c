#include <digitor/digitor.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Compile-time coverage of public fields without assuming platform padding. */
_Static_assert(sizeof(((DigitorTextureDesc*)0)->width) == sizeof(uint32_t),
               "texture width must be uint32_t-sized");
_Static_assert(sizeof(((DigitorTextureDesc*)0)->height) == sizeof(uint32_t),
               "texture height must be uint32_t-sized");
_Static_assert(sizeof(DigitorTextureDesc) >=
                   sizeof(uint32_t) * 3u + sizeof(DigitorPixelFormat),
               "texture descriptor is too small for its public fields");
_Static_assert(sizeof(((DigitorBufferDesc*)0)->size) == sizeof(uint64_t),
               "buffer size must be uint64_t-sized");
_Static_assert(sizeof(DigitorBufferDesc) >= sizeof(uint64_t) + sizeof(uint32_t),
               "buffer descriptor is too small for its public fields");
_Static_assert(sizeof(((DigitorRendererInfo*)0)->backend_name) == 64u,
               "backend_name public array size changed");
_Static_assert(sizeof(((DigitorRendererInfo*)0)->device_name) == 128u,
               "device_name public array size changed");

int main(void) {
    DigitorTextureDesc texture = {
        7u, 5u, DIGITOR_PIXEL_FORMAT_RGBA8_UNORM,
        DIGITOR_TEXTURE_USAGE_SAMPLED | DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION
    };
    DigitorBufferDesc buffer = {
        UINT64_C(4096), DIGITOR_BUFFER_USAGE_STORAGE | DIGITOR_BUFFER_USAGE_UPLOAD
    };

    assert(texture.width == 7u);
    assert(texture.height == 5u);
    assert(texture.format == DIGITOR_PIXEL_FORMAT_RGBA8_UNORM);
    assert(texture.usage == (DIGITOR_TEXTURE_USAGE_SAMPLED |
                             DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION));
    assert(buffer.size == UINT64_C(4096));
    assert(buffer.usage == (DIGITOR_BUFFER_USAGE_STORAGE | DIGITOR_BUFFER_USAGE_UPLOAD));
    DigitorResult (*map_buffer)(DigitorBuffer*, uint64_t, uint64_t, void**) = digitor_map_buffer;
    DigitorResult (*unmap_buffer)(DigitorBuffer*) = digitor_unmap_buffer;
    (void)map_buffer;
    (void)unmap_buffer;

    /* Referencing these values verifies that the public enums remain available to C. */
    assert(DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT);
    assert(DIGITOR_RENDERER_CPU != DIGITOR_RENDERER_AUTO);
    return 0;
}
