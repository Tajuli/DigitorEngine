#include "digitor/windows_zero_copy_rollout.hpp"

#include <fstream>
#include <sstream>

namespace digitor {

WindowsZeroCopyRolloutDecision evaluate_windows_zero_copy_rollout(
    WindowsZeroCopyRolloutMode requested,
    const std::string& report_path,
    bool strict_gpu_first) noexcept {
  WindowsZeroCopyRolloutDecision d;
  d.requested=requested; d.strict_gpu_first=strict_gpu_first;
  if(requested==WindowsZeroCopyRolloutMode::disabled){
    d.effective=WindowsZeroCopyRolloutMode::disabled;
    d.diagnostic="Windows zero-copy disabled"; return d;
  }
  if(requested==WindowsZeroCopyRolloutMode::qualification_only){
    d.effective=requested; d.diagnostic="qualification-only mode"; return d;
  }
  std::ifstream file(report_path,std::ios::binary);
  if(!file){d.diagnostic="production mode blocked: qualification report missing";return d;}
  std::ostringstream text; text<<file.rdbuf(); d.report_loaded=true;
  const auto body=text.str();
  d.production_ready=body.find("\"production_ready\": true")!=std::string::npos;
  if(!d.production_ready){
    d.diagnostic="production mode blocked: qualification gates did not pass";
    return d;
  }
  if(!strict_gpu_first){
    d.diagnostic="production mode blocked: strict GPU-first policy is required";
    return d;
  }
  d.effective=WindowsZeroCopyRolloutMode::production;
  d.diagnostic="qualified Windows zero-copy production mode enabled";
  return d;
}

} // namespace digitor
