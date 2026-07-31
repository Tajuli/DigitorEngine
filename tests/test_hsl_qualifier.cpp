#include "digitor/qualifier.hpp"
#include "digitor/commands.hpp"
#include "gpu/gpu_backend.hpp"
#include "gpu/native_hsl_qualifier.hpp"

#include <atomic>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
class QualifierBackend final : public digitor::IRenderBackend {
public:
  bool initialize(bool) override { return true; }
  void shutdown() noexcept override {}
  DigitorRendererInfo info() const noexcept override {
    DigitorRendererInfo i{}; i.backend=DIGITOR_RENDERER_VULKAN;i.is_gpu=1;return i;
  }
protected:
  DigitorResult execute_process_hsl_qualifier_gpu(std::span<const digitor::Color>,
      std::uint32_t w,std::uint32_t h,std::int64_t ts,
      const digitor::HslQualifierParameters&,digitor::ProcessedGpuFramePtr& out) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_VULKAN,true,"fixture","fixture","hsl","hsl");
    provenance_.output_written=true;
    out=std::make_shared<digitor::ProcessedGpuFrame>(this,DIGITOR_RENDERER_VULKAN,
      digitor::GpuFrameMetadata{w,h,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
      digitor::GpuFrameAlpha::straight,ts,"linear-matte"},901,
      std::make_shared<int>(1),std::make_shared<std::atomic_bool>(true),true);
    return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_hsl_qualifier_gpu(const digitor::GpuSourceResource& s,
      std::int64_t ts,const digitor::HslQualifierParameters& p,
      digitor::ProcessedGpuFramePtr& out) noexcept override {
    return execute_process_hsl_qualifier_gpu({},s.width,s.height,ts,p,out);
  }
  DigitorResult execute_validation_readback_hsl_qualifier(
      const digitor::ProcessedGpuFramePtr& f,std::span<float> out) noexcept override {
    if(!f||out.size()!=static_cast<std::size_t>(f->metadata().width)*f->metadata().height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    for(auto& v:out)v=0.5f;provenance_.readback_performed=true;return DIGITOR_RESULT_OK;
  }
};
}

void test_hsl_qualifier() {
  using namespace digitor;
  reset_hsl_qualifier_reference_count();
  const auto identity=HslQualifierParameters::create();
  assert(identity->is_identity() && identity->serialize()==identity->identity());
  QualifierSettings settings;
  settings.hue={0.95f,0.05f,0.05f};
  settings.saturation={0.5f,1.0f,0.1f};
  const auto red=HslQualifierParameters::create(settings);
  assert(apply_hsl_qualifier_reference({1,0,0,1},*red)>.99f);
  assert(apply_hsl_qualifier_reference({0,1,0,1},*red)<.01f);
  settings.invert=true;
  const auto inverted=HslQualifierParameters::create(settings);
  assert(apply_hsl_qualifier_reference({1,0,0,1},*inverted)<.01f);
  const auto native=native_hsl_qualifier_parameters(*inverted,7,5);
  assert(native.pixel_count==35 && native.width==7 && native.height==5);
  assert((native.flags&hsl_qualifier_flag_invert)!=0);
  assert(!hsl_qualifier_shader_source().empty());
  bool bad=false;auto invalid=settings;invalid.blur=std::numeric_limits<float>::quiet_NaN();
  try{(void)HslQualifierParameters::create(invalid);}catch(const std::invalid_argument&){bad=true;}
  assert(bad);

  auto cleanup=settings;cleanup.blur=1.0f;
  const auto blurred=HslQualifierParameters::create(cleanup);
  const auto native_blurred=native_hsl_qualifier_parameters(*blurred,7,5);
  assert(native_blurred.cleanup.w==1.0f && native_blurred.cleanup.z==0.0f);
  cleanup.blur=0.0f;cleanup.denoise=0.5f;
  const auto denoised=HslQualifierParameters::create(cleanup);
  const auto native_denoised=native_hsl_qualifier_parameters(*denoised,7,5);
  assert(native_denoised.cleanup.z==0.5f && native_denoised.cleanup.w==0.0f);

  HslQualifier legacy;legacy.set_settings(settings);
  std::vector<Color> input{{1,0,0,1},{0,1,0,1}};std::vector<float> matte(2,-1);
  CommandBuffer buffer;CommandEncoder encoder(buffer);bad=false;
  try{legacy.matte_gpu(encoder,input,matte,2,1);}catch(const std::logic_error&){bad=true;}
  assert(bad && matte[0]==-1 && matte[1]==-1);

  QualifierBackend backend;ProcessedGpuFramePtr frame;
  const auto before=hsl_qualifier_reference_count();
  assert(backend.process_hsl_qualifier_gpu(input,2,1,12,*red,frame)==DIGITOR_RESULT_OK);
  assert(frame && frame->ready() && hsl_qualifier_reference_count()==before);
  std::vector<float> readback(2);
  assert(backend.validation_readback_hsl_qualifier(frame,readback)==DIGITOR_RESULT_OK);
  assert(readback[0]==.5f && readback[1]==.5f);
}
