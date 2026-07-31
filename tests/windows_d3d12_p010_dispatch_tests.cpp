#include "digitor/windows_d3d12_p010_dispatch.hpp"
#include <cassert>

int main(){
  using namespace digitor;
  WindowsD3D12P010DispatchConfig invalid{};
  WindowsD3D12P010Dispatch dispatch(invalid);
  assert(dispatch.initialize()==DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(dispatch.gpu_only());
  const auto telemetry=dispatch.telemetry();
  assert(telemetry.cpu_copies==0);
  WindowsP010GpuConstants constants{};
  assert(dispatch.dispatch(nullptr,nullptr,constants,nullptr,nullptr,0)==DIGITOR_RESULT_INVALID_ARGUMENT);
  return 0;
}
