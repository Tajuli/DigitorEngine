#include "digitor/windows_zero_copy_import.hpp"

#include <cassert>
#include <memory>

using namespace digitor;

namespace {
NativeMediaSurfacePtr lifetime() {
  NativeMediaSurfaceDescriptor d;
  d.platform = NativeMediaPlatform::windows;
  d.handle_type = NativeMediaHandleType::dxgi_shared_handle;
  d.pixel_format = NativeMediaPixelFormat::nv12;
  d.width = 1920;
  d.height = 1080;
  d.plane_count = 2;
  d.native_handle = 1;
  return std::make_shared<NativeMediaSurface>(
      d, std::static_pointer_cast<void>(std::make_shared<int>(1)));
}
}

int main() {
  WindowsZeroCopySurface valid;
  valid.width = 1920;
  valid.height = 1080;
  valid.shared_handle = 1;
  valid.format = WindowsZeroCopyFormat::nv12;
  valid.color.matrix = WindowsYuvMatrix::bt709;
  valid.lifetime = lifetime();

  std::string diagnostic;
  assert(validate_windows_zero_copy_surface(valid, &diagnostic));
  assert(diagnostic.empty());

  auto odd = valid;
  odd.width = 1919;
  assert(!validate_windows_zero_copy_surface(odd, &diagnostic));

  auto missing_lifetime = valid;
  missing_lifetime.lifetime.reset();
  assert(!validate_windows_zero_copy_surface(missing_lifetime, &diagnostic));

  auto p010 = valid;
  p010.format = WindowsZeroCopyFormat::p010;
  p010.color.matrix = WindowsYuvMatrix::bt2020_ncl;
  assert(validate_windows_zero_copy_surface(p010, &diagnostic));

  return 0;
}
