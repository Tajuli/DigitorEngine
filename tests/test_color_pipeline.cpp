#include "digitor/color_pipeline.hpp"
#include "digitor/qualifier.hpp"
#include "digitor/video_texture.hpp"
#include <cassert>
#include <cmath>
#include <vector>
void test_color_pipeline(){using namespace digitor;
 std::vector<uint8_t> rgba{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};DecodedImage image{2,2,PixelFormat::rgba8,ColorRange::full,{{rgba,8},{},{}}};auto reference=convert_to_linear_rgba(image);for(auto b:{DIGITOR_RENDERER_VULKAN,DIGITOR_RENDERER_D3D12,DIGITOR_RENDERER_METAL,DIGITOR_RENDERER_OPENGL_ES}){auto t=upload_video_texture(b,image);assert(validate_pixels(reference,t.rgba_reference).passed);}
 std::vector<uint8_t> bgra{0,0,255,255};DecodedImage bi{1,1,PixelFormat::bgra8,ColorRange::full,{{bgra,4},{},{}}};assert(convert_to_linear_rgba(bi)[0].r==1);
 std::vector<uint8_t> y{128,128,128,128},uv{128,128};DecodedImage ni{2,2,PixelFormat::nv12,ColorRange::limited,{{y,2},{uv,2},{}}};assert(convert_to_linear_rgba(ni).size()==4);std::vector<uint8_t> u{128},v{128};DecodedImage yi{2,2,PixelFormat::yuv420p,ColorRange::limited,{{y,2},{u,1},{v,1}}};assert(convert_to_linear_rgba(yi).size()==4);
 ColorPipelineParameters p;p.primary.exposure=.25f;p.primary.contrast=1.1f;p.red=Curve({{0,0},{.5f,.4f},{1,1}});ColorShaderGraph graph;std::vector<Color> cpu(4),gpu(4);graph.process_cpu(reference,cpu,p);CommandBuffer cb;CommandEncoder enc(cb);bool rejected=false;try{graph.process_gpu(enc,reference,gpu,p);}catch(const std::runtime_error&){rejected=true;}assert(rejected);assert(graph.schedule().size()==4&&graph.shader_cache_size()==0&&graph.pipeline_cache_size()==0);
 HslQualifier qualifier;QualifierSettings qs;qs.hue={0,1,.1f};qs.saturation={0,1,.1f};qs.luminance={0,1,.1f};qs.blur=1;qualifier.set_settings(qs);auto cm=qualifier.matte_cpu(reference,2,2);std::vector<float> gm(4);CommandQueue q;CommandBuffer qb;CommandEncoder qe(qb);qualifier.matte_gpu(qe,reference,gm,2,2);qe.finish();q.submit(qb);for(size_t n=0;n<4;n++)assert(std::abs(cm[n]-gm[n])<1e-7f);
}
