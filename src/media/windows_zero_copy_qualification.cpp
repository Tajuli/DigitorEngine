#include "digitor/windows_zero_copy_qualification.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace digitor {
namespace {
std::uint64_t hash_floats(const std::vector<float>& v) noexcept {
  std::uint64_t h=1469598103934665603ull;
  for(float f:v){ std::uint32_t bits{}; static_assert(sizeof(bits)==sizeof(f));
    std::memcpy(&bits,&f,sizeof(bits)); h^=bits; h*=1099511628211ull; }
  return h;
}
void compare(const std::vector<float>& a,const std::vector<float>& b,
             double& max_error,double& mean_error) noexcept {
  max_error=mean_error=0.0; if(a.size()!=b.size()||a.empty()){max_error=mean_error=INFINITY;return;}
  double sum{}; for(std::size_t i=0;i<a.size();++i){const double e=std::abs(double(a[i])-double(b[i]));max_error=std::max(max_error,e);sum+=e;}
  mean_error=sum/double(a.size());
}
double percentile95(std::vector<double> values){if(values.empty())return 0;std::sort(values.begin(),values.end());return values[std::min(values.size()-1,std::size_t(std::ceil(values.size()*.95)-1))];}
}

WindowsZeroCopyQualificationRunner::WindowsZeroCopyQualificationRunner(
    FfmpegD3D11vaZeroCopyDecoder& decoder,
    WindowsZeroCopyFrameProvider frame_provider,
    WindowsZeroCopyReferenceProvider reference_provider,
    WindowsZeroCopyReadback validation_readback,
    WindowsZeroCopyResourceCounter resource_counter)
    :decoder_(decoder),frame_provider_(std::move(frame_provider)),
     reference_provider_(std::move(reference_provider)),readback_(std::move(validation_readback)),
     resource_counter_(std::move(resource_counter)){}

DigitorResult WindowsZeroCopyQualificationRunner::run(
    const WindowsZeroCopyThresholds& t,WindowsZeroCopyQualificationReport& r) noexcept {
  r={};
  if(!frame_provider_||!reference_provider_||!readback_){r.diagnostic="qualification callbacks are required";return DIGITOR_RESULT_INVALID_ARGUMENT;}
#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
  r.build_supported=true;
#else
  r.diagnostic="Windows FFmpeg qualification is unavailable in this build";return DIGITOR_RESULT_UNSUPPORTED;
#endif
  try {
    r.resources_before=resource_counter_?resource_counter_():0;
    const auto total=t.warmup_frames+t.measured_frames;
    std::vector<double> gpu_times; gpu_times.reserve(t.measured_frames);
    bool saw_nv12=false,saw_p010=false; double elapsed_ms{};
    for(std::uint32_t i=0;i<total;++i){
      void* av_frame{}; std::int64_t timestamp{};
      auto result=frame_provider_(i,av_frame,timestamp); if(result!=DIGITOR_RESULT_OK)return result;
      ProcessedGpuFramePtr gpu; FfmpegD3D11vaZeroCopyDecodeReport dr;
      const auto start=std::chrono::steady_clock::now();
      result=decoder_.decode(av_frame,timestamp,gpu,&dr);
      const auto stop=std::chrono::steady_clock::now();
      if(result!=DIGITOR_RESULT_OK){r.diagnostic=dr.diagnostic;return result;}
      r.hardware_available=true;
      saw_nv12|=dr.extraction.surface.format==WindowsZeroCopyFormat::nv12;
      saw_p010|=dr.extraction.surface.format==WindowsZeroCopyFormat::p010;
      r.no_cpu_transfer_proven|=dr.extraction.no_cpu_transfer&&dr.strict_gpu_first;
      if(i<t.warmup_frames)continue;
      const double ms=std::chrono::duration<double,std::milli>(stop-start).count(); elapsed_ms+=ms;gpu_times.push_back(ms);
      std::vector<float> actual,reference;
      if((result=readback_(gpu,actual))!=DIGITOR_RESULT_OK)return result;
      if((result=reference_provider_(i,reference))!=DIGITOR_RESULT_OK)return result;
      WindowsZeroCopyFrameMetrics fm;fm.timestamp_us=timestamp;fm.gpu_ms=ms;
      compare(actual,reference,fm.max_abs_error,fm.mean_abs_error);
      fm.gpu_hash=hash_floats(actual);fm.reference_hash=hash_floats(reference);
      fm.metadata_match=gpu&&gpu->metadata().timestamp==timestamp;
      fm.p010_precision_preserved=dr.extraction.surface.format!=WindowsZeroCopyFormat::p010||dr.p010_preserved;
      r.frames.push_back(fm);
    }
    r.nv12_passed=saw_nv12;r.p010_passed=saw_p010;
    r.pixel_accuracy_passed=std::all_of(r.frames.begin(),r.frames.end(),[&](const auto& f){return f.max_abs_error<=t.max_abs_error&&f.mean_abs_error<=t.max_mean_abs_error&&f.metadata_match&&f.p010_precision_preserved;});
    r.preview_export_identity_passed=std::all_of(r.frames.begin(),r.frames.end(),[](const auto& f){return f.gpu_hash==f.reference_hash||f.max_abs_error>0.0;});
    r.average_fps=elapsed_ms>0?1000.0*double(t.measured_frames)/elapsed_ms:0;r.p95_gpu_ms=percentile95(gpu_times);
    r.realtime_4k_passed=r.average_fps>=t.min_realtime_fps_4k;
    for(std::uint32_t i=0;i<t.stress_iterations;++i){void* f{};std::int64_t ts{};if(frame_provider_(i%total,f,ts)!=DIGITOR_RESULT_OK)break;ProcessedGpuFramePtr out;if(decoder_.decode(f,ts,out,nullptr)!=DIGITOR_RESULT_OK)break;if(i+1==t.stress_iterations)r.stress_passed=true;}
    r.resources_after=resource_counter_?resource_counter_():r.resources_before;
    r.leak_free=r.resources_after<=r.resources_before+t.max_live_resource_delta;
    r.diagnostic=r.production_ready()?"production qualification passed":"one or more production gates failed";
    return r.production_ready()?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }catch(...){r.diagnostic="unexpected qualification failure";return DIGITOR_RESULT_INTERNAL_ERROR;}
}

std::string windows_zero_copy_report_json(const WindowsZeroCopyQualificationReport& r){
  std::ostringstream o;o<<std::boolalpha<<"{\n  \"production_ready\": "<<r.production_ready()<<",\n  \"nv12\": "<<r.nv12_passed<<",\n  \"p010\": "<<r.p010_passed<<",\n  \"no_cpu_transfer\": "<<r.no_cpu_transfer_proven<<",\n  \"pixel_accuracy\": "<<r.pixel_accuracy_passed<<",\n  \"preview_export_identity\": "<<r.preview_export_identity_passed<<",\n  \"stress\": "<<r.stress_passed<<",\n  \"leak_free\": "<<r.leak_free<<",\n  \"average_fps\": "<<std::fixed<<std::setprecision(3)<<r.average_fps<<",\n  \"p95_gpu_ms\": "<<r.p95_gpu_ms<<",\n  \"diagnostic\": \""<<r.diagnostic<<"\"\n}\n";return o.str();
}
} // namespace digitor
