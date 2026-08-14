#include "gpu/windows_d3d12_yuv_root_contract_internal.hpp"

#include <cassert>

int main() {
  using namespace digitor::internal;
  static_assert(sizeof(WindowsD3D12YuvConstantsLayout) == 80);
  static_assert(sizeof(WindowsD3D12YuvConstantsLayout) /
                    sizeof(std::uint32_t) ==
                20);
  assert(kWindowsD3D12YuvRootConstants.shader_register == 0);
  assert(kWindowsD3D12YuvRootConstants.register_space == 0);
  assert(kWindowsD3D12YuvRootConstants.num_32bit_values == 20);
  return 0;
}
