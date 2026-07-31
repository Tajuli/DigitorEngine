#include "digitor/windows_zero_copy_media_host.hpp"
#include "digitor/windows_zero_copy_rollout.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc,char** argv){
  if(argc<4){
    std::cerr<<"usage: digitor_windows_zero_copy_media <media> <report.json> <d3d12-device-hex>\n";
    return 64;
  }
  const auto raw=std::strtoull(argv[3],nullptr,16);
  digitor::WindowsZeroCopyMediaHostOptions options;
  options.media_path=argv[1]; options.report_path=argv[2];
  options.strict_gpu_first=true;
  digitor::WindowsZeroCopyMediaHost host(reinterpret_cast<void*>(raw),options);
  std::string diagnostic;
  auto result=host.open(&diagnostic);
  if(result!=DIGITOR_RESULT_OK){std::cerr<<diagnostic<<"\n";return 2;}
  digitor::WindowsZeroCopyThresholds thresholds;
  digitor::WindowsZeroCopyQualificationReport report;
  result=host.qualify(thresholds,report,&diagnostic);
  std::cout<<digitor::windows_zero_copy_report_json(report);
  const auto rollout=digitor::evaluate_windows_zero_copy_rollout(
      digitor::WindowsZeroCopyRolloutMode::production,argv[2],true);
  std::cerr<<rollout.diagnostic<<"\n";
  return result==DIGITOR_RESULT_OK&&rollout.production_ready?0:3;
}
