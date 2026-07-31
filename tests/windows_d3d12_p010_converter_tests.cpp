#include "digitor/windows_d3d12_p010_converter.hpp"

#include <cassert>
#include <cmath>

int main(){
  using namespace digitor;
  WindowsP010ConversionConfig invalid{};
  WindowsD3D12P010Converter converter(invalid);
  assert(converter.initialize()==DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(converter.gpu_only());
  assert(converter.telemetry().cpu_copies==0);

  WindowsD3D12FrameLease lease;
  WindowsP010EncoderSurface surface;
  assert(converter.convert(lease,surface)==DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(!surface.resource && !surface.lifetime);

  WindowsP010ConversionConfig limited{};
  limited.width=3840; limited.height=2160;
  limited.matrix=WindowsOutputMatrix::bt709;
  limited.transfer=WindowsOutputTransfer::gamma24;
  limited.full_range=false;
  const auto a=windows_p010_gpu_constants(limited);
  assert(a.width==3840 && a.height==2160);
  assert(a.y_offset==64.0f && a.y_scale==876.0f);
  assert(a.uv_offset==512.0f && a.uv_scale==896.0f);
  assert(std::fabs(a.rgb_to_yuv[0]-0.2126f)<1e-6f);

  auto hdr=limited;
  hdr.matrix=WindowsOutputMatrix::bt2020_ncl;
  hdr.transfer=WindowsOutputTransfer::pq;
  hdr.full_range=true;
  hdr.mastering_peak_nits=1000.0f;
  const auto b=windows_p010_gpu_constants(hdr);
  assert(b.y_offset==0.0f && b.y_scale==1023.0f);
  assert(b.transfer==static_cast<unsigned>(WindowsOutputTransfer::pq));
  assert(b.mastering_peak_nits==1000.0f);
  assert(std::fabs(b.rgb_to_yuv[0]-0.2627f)<1e-6f);
  return 0;
}
