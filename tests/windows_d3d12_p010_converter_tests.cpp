#include "digitor/windows_d3d12_p010_converter.hpp"

#include <cassert>

int main(){
  using namespace digitor;
  WindowsP010ConversionConfig invalid{};
  WindowsD3D12P010Converter converter(invalid);
  assert(converter.initialize()==DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(converter.gpu_only());
  const auto telemetry=converter.telemetry();
  assert(telemetry.cpu_copies==0);

  WindowsD3D12FrameLease lease;
  WindowsP010EncoderSurface surface;
  assert(converter.convert(lease,surface)==DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(!surface.resource && !surface.lifetime);

  WindowsP010GpuConstants constants{};
  assert(sizeof(constants.rgb_to_yuv)/sizeof(constants.rgb_to_yuv[0])==12);
  static_assert(static_cast<unsigned>(WindowsOutputMatrix::bt709)!=
                static_cast<unsigned>(WindowsOutputMatrix::bt2020_ncl));
  static_assert(static_cast<unsigned>(WindowsOutputTransfer::pq)!=
                static_cast<unsigned>(WindowsOutputTransfer::hlg));
  return 0;
}
