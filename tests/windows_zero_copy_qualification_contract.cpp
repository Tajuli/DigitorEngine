#include "digitor/windows_zero_copy_qualification.hpp"
#include <cassert>

int main(){
  digitor::WindowsZeroCopyQualificationReport r;
  assert(!r.production_ready());
  r.build_supported=r.hardware_available=r.nv12_passed=r.p010_passed=true;
  r.no_cpu_transfer_proven=r.pixel_accuracy_passed=true;
  r.preview_export_identity_passed=r.stress_passed=r.leak_free=true;
  r.realtime_4k_passed=true;
  assert(r.production_ready());
  const auto json=digitor::windows_zero_copy_report_json(r);
  assert(json.find("\"production_ready\": true")!=std::string::npos);
  digitor::WindowsZeroCopyThresholds t;
  assert(t.max_abs_error<=2.0/1023.0);
  assert(t.measured_frames>=300);
  assert(t.stress_iterations>=2000);
  return 0;
}
