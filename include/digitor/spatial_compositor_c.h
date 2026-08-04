#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorPixelF { float r,g,b,a; } DigitorPixelF;
typedef struct DigitorSpatialParams {
  double position_x,position_y,scale_x,scale_y,anchor_x,anchor_y;
  double rotation_degrees,opacity;
  double crop_left,crop_top,crop_right,crop_bottom;
  int flip_x,flip_y,bilinear;
} DigitorSpatialParams;
typedef struct DigitorChromaParams { double r,g,b,similarity,softness,spill; int enabled; } DigitorChromaParams;

enum { DIGITOR_SPATIAL_OK=0, DIGITOR_SPATIAL_INVALID_ARGUMENT=1, DIGITOR_SPATIAL_GPU_UNAVAILABLE=2, DIGITOR_SPATIAL_INTERNAL_ERROR=3 };

int digitor_render_spatial_rgba32f(const DigitorPixelF* input,uint32_t input_width,uint32_t input_height,
                                   DigitorPixelF* output,uint32_t output_width,uint32_t output_height,
                                   const DigitorSpatialParams* params,int backend,int gpu_available,int allow_cpu_fallback);
int digitor_apply_chroma_key_rgba32f(DigitorPixelF* pixels,uint32_t width,uint32_t height,const DigitorChromaParams* params);
#ifdef __cplusplus
}
#endif
