#include "digitor/windows_zero_copy_qualification.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv){
  if(argc<3){
    std::cerr<<"usage: digitor_zero_copy_qualify <nv12-or-p010-media> <report.json>\n";
    return 2;
  }
  // Hardware callbacks are intentionally supplied by the Windows host harness.
  // This executable is the stable report/exit-code surface used by local labs
  // and self-hosted GitHub Actions runners.
  digitor::WindowsZeroCopyQualificationReport report;
  report.diagnostic=std::string("host harness not registered for input: ")+argv[1];
  std::ofstream output(argv[2],std::ios::binary);
  if(!output){std::cerr<<"cannot create report\n";return 3;}
  output<<digitor::windows_zero_copy_report_json(report);
  std::cerr<<report.diagnostic<<"\n";
  return report.production_ready()?0:1;
}
